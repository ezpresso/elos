/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 *
 * Copyright (c) 2017, Elias Zell
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <kern/system.h>
#include <kern/init.h>
#include <kern/atomic.h>
#include <kern/futex.h>
#include <vm/malloc.h>
#include <vm/slab.h>
#include <vfs/dev.h>
#include <block/block.h>
#include <sys/limits.h>

static DEFINE_VM_SLAB(blk_req_slab, sizeof(blk_req_t), 0);
static DEFINE_VM_SLAB(blk_handler_slab, sizeof(blk_handler_t), 0);
rwlock_t blk_lock = RWLOCK_INIT;

blk_object_t *blk_object_new(blk_class_t *class, void *priv) {
	blk_object_t *obj;

	rwlock_assert(&blk_lock, RWLOCK_WR);

	rwunlock(&blk_lock);
	obj = kmalloc(sizeof(*obj), VM_WAIT);
	wrlock(&blk_lock);

	list_init(&obj->providers);
	list_init(&obj->consuming);
	obj->class = class;
	obj->priv = priv;
	obj->depth = 0;

	return obj;
}

static void blk_object_free(blk_object_t *obj) {
	blk_provider_t *prov;

	rwlock_assert(&blk_lock, RWLOCK_WR);
	if(obj->class->destroy) {
		obj->class->destroy(obj);
	}

	foreach(prov, &obj->consuming) {
		blk_disuse_provider(prov);
	}

	list_destroy(&obj->providers);
	list_destroy(&obj->consuming);

	/*
	 * TODO consider unlock()
	 * OR finally implement some sort of asynchronous kfree
	 */
	kfree(obj);
}

void blk_object_destroy_pr(blk_object_t *obj) {
	blk_provider_t *prov;

	rwlock_assert(&blk_lock, RWLOCK_WR);

	/*
	 * Remove all of the providers this object exports.
	 */
	foreach(prov, &obj->providers) {
		blk_provider_destroy(prov);
	}
}

void blk_object_destroy(blk_object_t *obj) {
	rwlock_assert(&blk_lock, RWLOCK_WR);

	/*
	 * Remove all of the providers this object exports.
	 */
	blk_object_destroy_pr(obj);

	/*
	 * Don't free the object if somebody still has a reference
	 * to one of the providerds.
	 */
	if(list_length(&obj->providers) == 0) {
		blk_object_free(obj);
	}
}

blk_provider_t *blk_provider_new(blk_object_t *obj, void *priv) {
	blk_provider_t *prov;

	rwlock_assert(&blk_lock, RWLOCK_WR);

	rwunlock(&blk_lock);
	prov = kmalloc(sizeof(*prov), VM_WAIT);
	wrlock(&blk_lock);

	list_node_init(prov, &prov->node);
	list_node_init(prov, &prov->user_node);
	ref_init(&prov->ref);
	prov->obj = obj;
	prov->priv = priv;
	prov->dev = 0;
	prov->flags = 0;
	prov->user = NULL;
	prov->cache = NULL;
	list_append(&obj->providers, &prov->node);

	return prov;
}

void blk_provider_unref(blk_provider_t *pr) {
	blk_object_t *obj;

	if(ref_dec(&pr->ref) == false) {
		return;
	}

	/*
	 * The removal of block providers was never extensively tested.
	 */
	kpanic("TODO test");
	assert(F_ISSET(pr->flags, BLK_P_RMV));

	/*
	 * If every provider of an object is removed, the object itself
	 * can be freed.
	 */
	obj = pr->obj;
	wrlocked(&blk_lock) {
		obj = pr->obj;
		if(list_remove(&obj->providers, &pr->node) == true) {
			blk_object_free(obj);
		}
	}

	list_node_destroy(&pr->node);
	list_node_destroy(&pr->user_node);
	kfree(pr);
}

void blk_provider_destroy(blk_provider_t *pr) {
	rwlock_assert(&blk_lock, RWLOCK_WR);

	/*
	 * Don't allow any further access through devfs.
	 */
	if(F_ISSET(pr->flags, BLK_P_DEVFS)) {
		blk_file_destroy(pr);
	}

	F_SET(pr->flags, BLK_P_RMV);
	if(pr->user) {
		/*
		 * Inform the user. This provider will be freed when the user
		 * calls blk_provider_disuse.
		 */
		pr->user->class->prov_lost(pr->user, pr);
	}

	blk_provider_unref(pr);
}

int blk_use_provider(blk_provider_t *pr, blk_object_t *obj) {
	int err;

	rwlock_assert(&blk_lock, RWLOCK_WR);

	if(F_ISSET(pr->flags, BLK_P_INUSE)) {
		return -EBUSY;
	} else if(pr->obj->depth == BLK_MAXDEPTH) {
		return -ELOOP;
	} else if(F_ISSET(pr->flags, BLK_P_RMV)) {
		return -ENXIO;
	}

	/*
	 * Don't allow any further access to devfs entry. If the device
	 * is currently opened for write access, this operation fails.
	 */
	if(F_ISSET(pr->flags, BLK_P_DEVFS)) {
		err = dev_prot(pr->dev);
		if(err) {
			return err;
		}
	}

	blk_provider_ref(pr);
	F_SET(pr->flags, BLK_P_INUSE);

	/*
	 * TODO once filesystems get a real blk_object and not a
	 * NULL, this can be simplified.
	 */
	if(obj != NULL) {
		list_append(&obj->consuming, &pr->user_node);
		obj->depth = pr->obj->depth + 1;
		pr->user = obj;
	}

	return 0;
}

void blk_disuse_provider(blk_provider_t *pr) {
	rwlock_assert(&blk_lock, RWLOCK_WR);

	assert(F_ISSET(pr->flags, BLK_P_INUSE));
	if(pr->user) {
		list_remove(&pr->user->consuming, &pr->user_node);
		pr->user = NULL;
	}

	F_CLR(pr->flags, BLK_P_INUSE);
	blk_provider_unref(pr);
}

void blk_req_init(blk_req_t *req, blk_provider_t *pr, blk_handler_t *hand,
	blk_rtype_t type, int flags)
{
	atomic_inc_relaxed(&hand->num);
	list_node_init(req, &req->node);
	req->pr = pr;
	req->type = type;
	req->flags = flags;
	req->handler = hand;
}

void blk_req_uninit(blk_req_t *req) {
	list_node_destroy(&req->node);
}

blk_req_t *blk_req_new(blk_provider_t *pr, blk_handler_t *hand,
	blk_rtype_t type, int flags, vm_flags_t alloc_flags)
{
	blk_req_t *req;

	assert(!F_ISSET(hand->flags, BLK_HAND_SETUP));

	/*
	 * TODO
	 * When the system runs out of memory, it would be fatal if the swap
	 * code had to allocating memory for block I/O. However swap is designed
	 * to work asynchronous, we will have to allocate from reserved memory
	 * here in certain cases (once VM_RESERVED is implemented of course)
	 *
	 * VM_FLAGS_CHECK(alloc_flags, VM_RESERVED);
	 */
	req = vm_slab_alloc(&blk_req_slab, alloc_flags | VM_WAIT);
	blk_req_init(req, pr, hand, type, flags);

	return req;
}

void blk_req_free(blk_req_t *req) {
	blk_req_uninit(req);
	vm_slab_free(&blk_req_slab, req);
}

int blk_req_forward(blk_object_t *obj, blk_req_t *req) {
	rwlock_assert(&blk_lock, RWLOCK_RD);

	(void) obj;
	(void) req;
#if 0
	blk_provider_t *pr = obj->provider;
#endif

	return 0;
}

void blk_handler_init(blk_handler_t *hand, int flags) {
	kassert((flags & ~BLK_HAND_ASYNC) == 0, "[block] init handler: invalid "
		"flags: %d", flags);

	sync_init(&hand->lock, SYNC_SPINLOCK);
	hand->err = 0;
	hand->num = 0;
	hand->done = 0;
	hand->flags = flags;
}

void blk_handler_uninit(blk_handler_t *hand) {
	assert(hand->flags & BLK_HAND_SETUP);
	sync_destroy(&hand->lock);
}

blk_handler_t *blk_handler_new(int flags) {
	blk_handler_t *hand;

	/* TODO VM_RESERVED */
	hand = vm_slab_alloc(&blk_handler_slab, VM_WAIT);
	blk_handler_init(hand, flags);
	return hand;
}

void blk_handler_free(blk_handler_t *hand) {
	blk_handler_uninit(hand);
	vm_slab_free(&blk_handler_slab, hand);
}

int blk_handler_start(blk_handler_t *hand) {
	size_t num;
	int err;

	sync_acquire(&hand->lock);
	F_SET(hand->flags, BLK_HAND_SETUP);

	if(F_ISSET(hand->flags, BLK_HAND_ASYNC)) {
		/*
		 * The requests have already been launched and thus it is
		 * possible that they are finished already.
		 */
		if(hand->num == hand->done) {
			sync_release(&hand->lock);
			blk_event_add(&hand->event);
		} else {
			sync_release(&hand->lock);
		}

		return 0;
	} else {
		/*
		 * Wait until all the requests are finished.
		 */
		while((num = hand->done) != hand->num) {
			sync_release(&hand->lock);
			kern_wait(&hand->done, num, 0);
			sync_acquire(&hand->lock);
		}

		err = hand->err;
		sync_release(&hand->lock);

		return err;
	}
}

void blk_abort(blk_handler_t *hand) {
	/*
	 * Claim to be fully setup, and wait for the
	 * started requests to finish.
	 */
	synchronized(&hand->lock) {
		F_CLR(hand->flags, BLK_HAND_ASYNC);
	}

	blk_handler_start(hand);
}

void blk_req_done(blk_req_t *req, int err) {
	blk_handler_t *hand;
	int flags;

	hand = req->handler;
	flags = req->flags;

	sync_acquire(&hand->lock);
	hand->done++;
	if(err) {
		hand->err = err;
	}

	/*
	 * Not still there yet, waiting for more requests to complete.
	 */
	if(hand->done < hand->num) {
		sync_release(&hand->lock);
	} else {
		/*
		 * It is possible that all of the requests finished before
		 * calling blk_handler_start.
		 */
		if(!F_ISSET(hand->flags, BLK_HAND_SETUP)) {
			sync_release(&hand->lock);
		} else if(F_ISSET(hand->flags, BLK_HAND_ASYNC)) {
			sync_release(&hand->lock);
			blk_event_add(&hand->event);
		} else {
			/*
			 * Wakeup the thread waiting for completion before
			 * releasing the lock. After the lock is released,
			 * it's not safe to use the handler anymore.
			 */
			kern_wake(&hand->done, 1, 0);
			sync_release(&hand->lock);
		}
	}

	/*
	 * Free the request if the user requested this.
	 * Accessing req at this point in time is only
	 * safe if the AUTOFREE flag is set, because
	 * hand->lock is not held anymore. Thus the flags
	 * field was read before, so we don't risk a
	 * use-after-free error.
	 */
	if(F_ISSET(flags, BLK_REQ_AUTOFREE)) {
		blk_req_free(req);
	}
}

int blk_req_launch(blk_req_t *req) {
	int res = 0;

	/*
	 * Launch request and wait if necessary.
	 */
	rdlocked(&blk_lock) {
		if(F_ISSET(req->pr->flags, BLK_P_RMV)) {
			res = -EIO;
		} else {
			res = req->pr->obj->class->request(req->pr, req);
		}
	}

	if(res < 0) {
		blk_req_done(req, res);
	}

	return res;
}

int blk_io(blk_provider_t *pr, blk_rtype_t type, blkno_t no, blkcnt_t cnt,
	void *ptr)
{
	blk_handler_t hand;
	blk_req_t req;
	int err;

	blk_handler_init(&hand, 0);
	blk_req_init(&req, pr, &hand, type, 0);

	kassert(type == BLK_WR || type == BLK_RD, "[block] I/O: invalid I/O "
		"type: %d", type);
	req.io.blk = no;
	req.io.cnt = cnt;
	req.io.map = ptr;
	req.io.paddr = 0;

	/*
	 * Launch request and wait.
	 */
	err = blk_req_launch(&req);
	if(err == 0) {
		err = blk_handler_start(&hand);
	}

	blk_req_uninit(&req);
	blk_handler_uninit(&hand);

	return err;
}

void __init init_block(void) {
	/* TODO */
	extern void blk_cache_init(void);
	blk_cache_init();

	extern void blk_event_init(void);
	blk_event_init();
}

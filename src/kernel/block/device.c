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
#include <vm/malloc.h>
#include <lib/string.h>
#include <block/block.h>
#include <block/device.h>
#include <block/part.h>

static uint64_t blk_dev_unit_free = UINT64_MAX;
static blk_request_t blk_dev_request;
static blk_destroy_t blk_dev_destroy;
static blk_class_t blk_dev_class = {
	.name =		"DEV",
	.create =	NULL,
	.request =	blk_dev_request,
	.destroy =	blk_dev_destroy,
	.prov_lost =	NULL,
};

static void blk_req_queue_callback(void *arg) {
	blk_req_queue_t *queue = arg;
	blk_dev_t *dev;
	blk_req_t *req;
	int err;

	rwlock_assert(&blk_lock, RWLOCK_RD);
	sync_scope_acquire(&queue->lock);
	assert(queue->running);

	while(queue->avail > 0 && (req = list_pop_front(&queue->pending))) {
		queue->avail--;
		sync_release(&queue->lock);

		dev = blk_provider_priv(req->pr);
		err = dev->start(dev, req);
		if(err) {
			blk_req_done(req, err);
			sync_acquire(&queue->lock);
			queue->avail++;
		} else {
			sync_acquire(&queue->lock);
		}
	}

	queue->running = false;
}

static void blk_req_queue_done(blk_req_queue_t *queue) {
	sync_scope_acquire(&queue->lock);
	assert(queue->avail < queue->maxreq);

	queue->avail++;
	if(list_first(&queue->pending) && queue->running == false) {
		queue->running = true;
		blk_event_add(&queue->event);
	}
}

static int blk_dev_request(blk_provider_t *pr, blk_req_t *req) {
	blk_dev_t *dev = blk_provider_priv(pr);
	blk_req_queue_t *queue = dev->req_queue;
	int err;

	rwlock_assert(&blk_lock, RWLOCK_RD);
	assert(req->pr == pr);
	assert(pr->priv);

	sync_acquire(&queue->lock);

	/*
	 * TODO maybe consider enqueing even if space is available to
	 * not starve other requests.
	 */
	if(queue->avail) {
		queue->avail--;
		sync_release(&queue->lock);
		err = dev->start(dev, req);
		if(err) {
			blk_req_queue_done(queue);
		}
	} else {
		/*
		 * TODO the I/O-scheduler should schedule the request.
		 */
		list_append(&queue->pending, &req->node);
		sync_release(&queue->lock);
		err = 0;
	}

	if(err == 0) {
		blk_provider_ref(pr);
	}

	return err;
}

blk_req_queue_t *blk_req_queue_alloc(size_t maxreq) {
	blk_req_queue_t *queue;

	queue = kmalloc(sizeof(*queue), VM_WAIT);
	blk_event_create(&queue->event, blk_req_queue_callback, queue);
	sync_init(&queue->lock, SYNC_MUTEX);
	list_init(&queue->pending);
	queue->avail = maxreq;
	queue->maxreq = maxreq;
	queue->running = false;

	return queue;
}

void blk_req_queue_free(blk_req_queue_t *queue) {
	assert(queue->avail == queue->maxreq);
	assert(!queue->running);

	blk_event_destroy(&queue->event);
	list_destroy(&queue->pending);
	sync_destroy(&queue->lock);
	kfree(queue);
}

blk_dev_t *blk_dev_alloc(void) {
	blk_dev_t *dev;

	dev = kmalloc(sizeof(*dev), VM_WAIT);
	dev->flags = 0;
	dev->name = NULL;
	dev->start = NULL;
	dev->req_queue = NULL;

	return dev;
}

void blk_dev_register(blk_dev_t *dev) {
	blk_provider_t *pr;
	blk_object_t *obj;

	assert(dev->blk_shift <= dev->pblk_shift);
	assert(dev->req_queue);
	wrlock_scope(&blk_lock);

	if(F_ISSET(dev->flags, BLK_DEV_MAPIO)) {
		kpanic("TODO blk dev mapped I/O");
	}

	/*
	 * TODO how to handle a 'run out of numbers' error?
	 */
	if(!dev->name) {
		dev->name = "disk";

		/*
		 * Get a new unit number. TODO could actually provide unit
		 * number allocation even if a name was provided.
		 */
		dev->unit = ffs(blk_dev_unit_free);
		if(!dev->unit) {
			return;
		} else {
			/*
			 * Remember that ffs returns result+1.
			 */
			dev->unit--;
			bclr(&blk_dev_unit_free, dev->unit);
		}
	}

	obj = blk_object_new(&blk_dev_class, dev);
	obj->blk_shift = dev->blk_shift;
	obj->pblk_shift = dev->pblk_shift;
	snprintf(obj->name, sizeof(obj->name), "%s%d", dev->name, dev->unit);

	dev->obj = obj;

	/*
	 * Don't allocate a provider yet, when no media is inserted.
	 */
	if(!F_ISSET(dev->flags, BLK_DEV_NOMEDIA)) {
		pr = blk_provider_new(obj, dev);
		strcpy(pr->name, obj->name);

		/*
		 * Create devfs entry.
		 */
		blk_file_new(pr);
	}

	kprintf("[block] registered device: %s\n"
		"\tblock size: %d\n"
		"\tphysical block size: %d\n"
		"\tnumber of blocks: %lld\n",
		obj->name, 1 << dev->blk_shift,
		1 << dev->pblk_shift, dev->blkcnt);

	/*
	 * Probe for partitions.
	 */
	/* TODO blk_attach(pr, &blk_part_class); */
}

static void blk_dev_destroy(blk_object_t *obj) {
	blk_dev_t *dev;

	/*
	 * Now that the providers are destroyed and no further requests
	 * can be made, the device-structure can be freed.
	 * The object itself will be freed automatically.
	 */
	dev = blk_object_priv(obj);
	kfree(dev);
}

void blk_dev_unregister(blk_dev_t *dev) {
	kpanic("TODO abort any outstanding requests");

	wrlock_scope(&blk_lock);
	blk_object_destroy(dev->obj);
}

void blk_dev_req_done(blk_req_t *req, int err) {
	blk_dev_t *dev = blk_provider_priv(req->pr);

	blk_req_queue_done(dev->req_queue);
	blk_provider_unref(req->pr);
	blk_req_done(req, err);
}

void blk_media_gone(blk_dev_t *dev) {
	blk_object_t *obj = dev->obj;

	kpanic("needs testing");

	/*
	 * Remove the provider.
	 */
	wrlock_scope(&blk_lock);
	assert(list_length(&obj->providers) == 1);

	if(!F_ISSET(dev->flags, BLK_DEV_NOMEDIA)) {
		F_SET(dev->flags, BLK_DEV_NOMEDIA);
		blk_object_destroy_pr(obj);
	}
}

void blk_media_inserted(blk_dev_t *dev) {
	blk_provider_t *pr;

	kpanic("needs testing");

	wrlock_scope(&blk_lock);

	/*
	 * Allocate a provider.
	 */
	assert(F_ISSET(dev->flags, BLK_DEV_NOMEDIA));
	F_CLR(dev->flags, BLK_DEV_NOMEDIA);

	pr = blk_provider_new(dev->obj, dev);
	strcpy(pr->name, dev->obj->name);
}

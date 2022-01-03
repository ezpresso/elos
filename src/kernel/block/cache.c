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
#include <kern/atomic.h>
#include <kern/futex.h>
#include <block/block.h>
#include <vm/slab.h>
#include <vm/reclaim.h>
#include <vm/malloc.h>
#include <lib/hashtab.h>
#include <lib/string.h>
#include <sys/limits.h>

#define BLKHASH_SIZE (1U << 16)
#define BLKHASH(cache, blk) ((uintptr_t)(cache) ^ (blk))

typedef struct blk_cache {
	blk_provider_t *pr;
	list_node_t node;
	rwlock_t lock;
	sync_t lru_lock;

	/**
	 * A list for keeping track of the block-cache entry usage.
	 * The oldest buffers are at the front of the list.
	 */
	list_t lru;
	size_t entry_num;
} blk_cache_t;

/**
 * A cache buffer. Some disks use a block size greater than the logical block
 * size. The device would have to do a RMW operation to allow writes not aligned
 * to the physical block size to remain backwards-compatible (i.e. dumb
 * bootloader expects 512 byte blocks). That's why the cache buffers store
 * physical blocks rather than logical blocks.
 */
typedef struct blk_cbuf {
	blk_cache_t *cache;
	list_node_t hnode; /* hash node */
	list_node_t lnode; /* provider node */
	rwlock_t lock;
	size_t ref;
	blkno_t no;
	void *data;

#define BLK_CBUF_OK	0
#define BLK_CBUF_ERR	1
	int status;
} blk_cbuf_t;

static DEFINE_VM_SLAB(blk_cbuf_cache, sizeof(blk_cbuf_t), 0);
static DEFINE_LIST(blk_cache_list);
static sync_t blk_cache_lock = SYNC_INIT(MUTEX);
static size_t blk_cache_bufs = 0;
static hashtab_t blk_cache_ht;

static inline blk_cbuf_t *blk_cbuf_alloc(blk_cache_t *cache, blkno_t no) {
	blk_cbuf_t *new;

	new = vm_slab_alloc(&blk_cbuf_cache, VM_WAIT);

	/*
	 * The buffer has the size of the physical device block size.
	 * Most of the hardware exports a 512byte interface while
	 * using e.g. 4kb internal block size. It would be inefficient
	 * to read 512 bytes in this case, because the device has to read
	 * the complete 4kb internally and then throw away 3584 bytes to
	 * emulate the 512 byte interface.
	 */
	new->data = kmalloc(1 << cache->pr->obj->pblk_shift, VM_WAIT);

	list_node_init(new, &new->hnode);
	list_node_init(new, &new->lnode);
	rwlock_init(&new->lock);
	new->cache = cache;
	new->ref = 1;
	new->no = no;

	atomic_inc_relaxed(&blk_cache_bufs);
	return new;
}

static inline void blk_cbuf_free(blk_cbuf_t *cbuf) {
	atomic_dec_relaxed(&blk_cache_bufs);

	list_node_destroy(&cbuf->hnode);
	list_node_destroy(&cbuf->lnode);
	rwlock_destroy(&cbuf->lock);

	kfree(cbuf->data);
	vm_slab_free(&blk_cbuf_cache, cbuf);
}

static inline size_t blk_cbuf_count(blk_cbuf_t *cbuf) {
	blk_object_t *obj = cbuf->cache->pr->obj;

	if(obj->pblk_shift > obj->blk_shift) {
		return 1 << (obj->pblk_shift - obj->blk_shift);
	} else {
		return 1;
	}
}

static void blk_cbuf_ref(blk_cbuf_t *cbuf) {
	sync_scope_acquire(&cbuf->cache->lru_lock);
	if(cbuf->ref++ == 0) {
		list_remove(&cbuf->cache->lru, &cbuf->lnode);
	}
}

static void blk_cbuf_unref(blk_cbuf_t *cbuf) {
	sync_scope_acquire(&cbuf->cache->lru_lock);
	if(--cbuf->ref == 0) {
		list_append(&cbuf->cache->lru, &cbuf->lnode);
	}
}

static blk_cbuf_t *blk_cache_lookup(blk_cache_t *cache, blkno_t no) {
	blk_cbuf_t *cbuf;
	size_t hash;

	rwlock_assert(&cache->lock, RWLOCK_RD);

	hash = BLKHASH(cache, no);
	hashtab_search(cbuf, hash, &blk_cache_ht) {
		if(cbuf->cache == cache && cbuf->no == no) {
			blk_cbuf_ref(cbuf);
			return cbuf;
		}
	}

	return NULL;
}

static int blk_cache_get(blk_cache_t *cache, blkno_t no, blk_cbuf_t **bufp) {
	blk_object_t *obj = cache->pr->obj;
	blk_cbuf_t *cbuf, *new;
	int err = 0;

	/*
	 * Align the logical sector size to physical sector size.
	 */
	no &= ~((1 << (obj->pblk_shift - obj->blk_shift)) - 1);

	rdlock(&cache->lock);
	cbuf = blk_cache_lookup(cache, no);
	rwunlock(&cache->lock);
	if(cbuf) {
		goto out;
	}

	/*
	 * A new buffer might be needed. Allocate the buffer after releasing
	 * the lock, for efficiency.
	 */
	new = blk_cbuf_alloc(cache, no);
	wrlock(&new->lock);
	wrlock(&cache->lock);

	/*
	 * Since the cache_lock was relased above, we have to check
	 * if the buffer is still not present.
	 */
	cbuf = blk_cache_lookup(cache, no);
	if(cbuf) {
		rwunlock(&cache->lock);
		rwunlock(&new->lock);
		blk_cbuf_free(new);
		goto out;
	}

	/*
	 * Add the buffer to the hashtable.
	 */
	cache->entry_num++;
	hashtab_set(&blk_cache_ht, BLKHASH(cache, no), &new->hnode);
	rwunlock(&cache->lock);

	/*
	 * Fill the buffer by reading data from the device.
	 * TODO NOT NECESSARY IN SOME CASES
	 */
	err = blk_read(cache->pr, no, blk_cbuf_count(new), new->data);
	if(err) {
		new->status = BLK_CBUF_ERR;
	} else {
		new->status = BLK_CBUF_OK;
	}

	rwunlock(&new->lock);
	if(err) {
		blk_cbuf_unref(new);
	}

	cbuf = new;

out:
	if(!err) {
		*bufp = cbuf;
	}

	return err;
}

#if notyet
int blk_getbuf(blk_provider_t *pr, size_t size, off_t off, blkbuf_t *buf) {
	size_t blkoff, blksz = 1 << pr->obj->pblk_shift;
	blk_cbuf_t *cbuf;
	blkno_t blk;
	int err;

	blk = off >> pr->obj->blk_shift;
	blkoff = off & ~(blksz - 1);
	assert(blkoff + size <= blksz);

	err = blk_cache_get(pr->cache, blk, &cbuf);
	if(err) {
		return err;
	}

	buf->cbuf = cbuf;
	buf->buffer = cbuf->data + blkoff;

	return 0;
}

void blk_putbuf(blkbuf_t *buf) {
	blk_cbuf_unref(buf->cbuf);
}
#endif

int bio(blk_provider_t *pr, blk_rtype_t type, off_t off, size_t size,
	void *ptr)
{
	size_t blkoff, blksz = 1 << pr->obj->pblk_shift;
	blk_cache_t *cache = pr->cache;

	assert(cache);
	assert(off >= 0);
	assert(size);

	blkoff = off & (blksz - 1);
	while(size) {
		blk_cbuf_t *cbuf;
		size_t cur;
		int err;

		cur = min(blksz - blkoff, size);
		err = blk_cache_get(cache, off >> pr->obj->blk_shift, &cbuf);
		if(err) {
			return err;
		}

		if(type == BLK_RD) {
			rdlock(&cbuf->lock);
		} else {
			wrlock(&cbuf->lock);
		}

		/*
		 * Check if the data insde the buffer is ok because this
		 * I/O request might depend on it.
		 */
		if((type == BLK_RD || cur != blksz) &&
			cbuf->status == BLK_CBUF_ERR)
		{
			err = -EIO;
		} else if(type == BLK_WR) {
			memcpy(cbuf->data + blkoff, ptr, cur);

			/*
			 * Writes happen immediately.
			 */
			err = blk_write(pr, cbuf->no, blk_cbuf_count(cbuf),
				cbuf->data);
			if(err) {
				cbuf->status = BLK_CBUF_ERR;
			} else {
				cbuf->status = BLK_CBUF_OK;
			}
		} else {
			memcpy(ptr, cbuf->data + blkoff, cur);
		}

		rwunlock(&cbuf->lock);
		blk_cbuf_unref(cbuf);
		if(err) {
			return err;
		}

		blkoff = 0;
		size -= cur;
		ptr += cur;
		off += cur;
	}

	return 0;
}

void blk_cache_add(blk_provider_t *pr) {
	blk_cache_t *cache;

	assert(pr->cache == NULL);
	cache = kmalloc(sizeof(*cache), VM_WAIT);

	list_node_init(cache, &cache->node);
	list_init(&cache->lru);
	rwlock_init(&cache->lock);
	sync_init(&cache->lru_lock, SYNC_MUTEX);
	cache->entry_num = 0;
	cache->pr = pr;

	sync_scope_acquire(&blk_cache_lock);
	list_append(&blk_cache_list, &cache->node);

	pr->cache = cache;
}

void blk_cache_rem(blk_provider_t *pr) {
	blk_cache_t *cache = pr->cache;
	blk_cbuf_t *cbuf;

	synchronized(&blk_cache_lock) {
		list_remove(&blk_cache_list, &cache->node);
	}

	/*
	 * Since no buffer should be in use anymore, all the
	 * blocks should be on the lru.
	 */
	foreach(cbuf, &cache->lru) {
		hashtab_remove(&blk_cache_ht, BLKHASH(cache, cbuf->no),
 			&cbuf->hnode);
		list_remove(&cache->lru, &cbuf->lnode);
		blk_cbuf_free(cbuf);
		cache->entry_num--;
	}

	assert(cache->entry_num == 0);
	kfree(cache);
	pr->cache = NULL;
}

static bool blk_cache_reclaim(void) {
	blk_cache_t *cache;

	sync_acquire(&blk_cache_lock);
	if(blk_cache_bufs == 0) {
		sync_release(&blk_cache_lock);
		return false;
	}

	foreach(cache, &blk_cache_list) {
		blk_cbuf_t *cbuf = NULL;

		wrlock(&cache->lock);

		/*
		 * Get the last recently used cached block.
		 */
		synchronized(&cache->lru_lock) {
			cbuf = list_pop_front(&cache->lru);
		}

		if(cbuf) {
			/*
			 * Remove the item from the cache.
			 */
			cache->entry_num--;
			hashtab_remove(&blk_cache_ht, BLKHASH(cache, cbuf->no),
				&cbuf->hnode);

			rwunlock(&cache->lock);
			sync_release(&blk_cache_lock);

			/*
			 * Free the block. The block does not need to be written
			 * to disk, because writes are performed immediately.
			 */
			blk_cbuf_free(cbuf);
			return true;
		} else {
			rwunlock(&cache->lock);
		}

		/*
		 * Continue searching for cached blocks.
		 */
		continue;
	}

	sync_release(&blk_cache_lock);
	return false;
}
vm_reclaim("blkdev-cache", blk_cache_reclaim);

void blk_cache_init(void) {
	hashtab_alloc(&blk_cache_ht, BLKHASH_SIZE, VM_WAIT);
}

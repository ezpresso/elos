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
#include <kern/rwlock.h>
#include <vfs/vcache.h>
#include <vfs/fs.h>
#include <vfs/vnode.h>
#include <vfs/vdirent.h>
#include <vm/reclaim.h>
#include <lib/list.h>
#include <lib/list-locked.h>
#include <lib/hashtab.h>
#include <lib/string.h>

static rwlock_t vn_lock = RWLOCK_INIT;
static DEFINE_LOCKLIST(vn_lru, MUTEX);
static hashtab_t vn_ht;
static rwlock_t vd_lock = RWLOCK_INIT;
static DEFINE_LOCKLIST(vd_lru, MUTEX);
static hashtab_t vd_ht;

static inline size_t vnode_cache_hash(filesys_t *fs, ino_t ino) {
	return (uintptr_t)(fs) ^ (ino);
}

static inline size_t vdirent_cache_hash(ino_t ino, const char *name) {
	size_t hash = 5381;
	int c;

	while((c = *name++)) {
		hash = ((hash << 5) + hash) + c;
	}

	return hash ^ ino;
}

vnode_t *vnode_cache_lookup(filesys_t *fs, ino_t ino) {
	size_t hash = vnode_cache_hash(fs, ino);
	vnode_t *node;

	sync_assert(&fs->lock);

	rdlock_scope(&vn_lock);
	hashtab_search(node, hash, &vn_ht) {
		if(node->fs == fs && node->ino == ino) {
			assert(node->nlink > 0);

			sync_scope_acquire(&vn_lru.lock);
			if(vnode_flags_clear(node, VN_VCLRU) & VN_VCLRU) {
				list_remove(&vn_lru.list, &node->lru_node);
			}

			return vnode_ref(node);
		}
	}

	return NULL;
}

void vnode_cache_add(vnode_t *node) {
	size_t hash;

	sync_assert(&node->fs->lock);
	hash = vnode_cache_hash(node->fs, node->ino);

	wrlock_scope(&vn_lock);
	hashtab_set(&vn_ht, hash, &node->node);
}

void vnode_cache_zeroref(vnode_t *node) {
	if(node->nlink == 0) {
		size_t hash;

		hash = vnode_cache_hash(node->fs, node->ino);
		wrlocked(&vn_lock) {
			hashtab_remove(&vn_ht, hash, &node->node);
		}

		sync_scope_acquire(&node->fs->lock);
		assert(!ref_get(&node->object.ref));
		filesys_vput(node->fs, node);
	} else if(!vnode_flags_test(node, VN_PERM)) {
		sync_scope_acquire(&node->object.lock);
		sync_scope_acquire(&vn_lru.lock);

		if(ref_get(&node->object.ref) == 0) {
			/*
			 * Pageout might have referenced the vnode without
			 * calling vnode_cache_lookup.
			 */
			if(!(vnode_flags_set(node, VN_VCLRU) & VN_VCLRU)) {
				list_append(&vn_lru.list, &node->lru_node);
			}
		}
	}
}

void vdirent_cache_add(vnode_t *node, vdirent_t *dirent) {
	size_t hash;

	VN_ASSERT_LOCK_WR(node);
	assert(dirent->owner == node->ino);

	hash = vdirent_cache_hash(dirent->owner, vdirent_name(dirent));

	wrlock_scope(&vd_lock);
	hashtab_set(&vd_ht, hash, &dirent->node);
	if(!F_ISSET(dirent->flags, VD_PERM)) {
		locklist_append(&vd_lru, &dirent->lru_node);
	}

}

vdirent_t *vdirent_cache_new(vnode_t *node, const char *name, size_t namelen,
	ino_t ino, int flags)
{
	vdirent_t *dirent;

	VN_ASSERT_LOCK_WR(node);
	dirent = vdirent_alloc(node->fs, name, namelen, node->ino, ino,
		flags, 0);
	if(likely(dirent != NULL)) {
		vdirent_cache_add(node, dirent);
	}

	return dirent;
}

vdirent_t *vdirent_cache_lookup(vnode_t *node, const char *name,
	size_t namelen)
{
	size_t hash;
	vdirent_t *dirent;

	VN_ASSERT_LOCK_RD(node);
	hash = vdirent_cache_hash(node->ino, name);

	rdlock_scope(&vd_lock);
	hashtab_search(dirent, hash, &vd_ht) {
		if(dirent->owner == node->ino &&
			dirent->fs == node->fs &&
			dirent->namelen == namelen &&
			!strcmp(vdirent_name(dirent), name))
		{
			if(!F_ISSET(dirent->flags, VD_PERM)) {
				locklist_remove(&vd_lru, &dirent->lru_node);
			}

			return dirent;
		}
	}

	return NULL;
}

void vdirent_cache_put(vnode_t *node, vdirent_t *dirent) {
	VN_ASSERT_LOCK_WR(node);

	if(!F_ISSET(dirent->flags, VD_PERM)) {
		locklist_append(&vd_lru, &dirent->lru_node);
	}
}

void vdirent_cache_rem(vnode_t *node, vdirent_t *dirent) {
	size_t hash;

	VN_ASSERT_LOCK_WR(node);
	hash = vdirent_cache_hash(node->ino, vdirent_name(dirent));

	wrlocked(&vd_lock) {
		hashtab_remove(&vd_ht, hash, &dirent->node);
	}

	/*
	 * TODO ADD A FLAG CALLED VM_FREE_LAZY, because we're
	 * currently holding the lock of the node....
	 */
	vdirent_free(dirent);
}

void vdirent_cache_try_rem(vnode_t *node, const char *name, size_t namelen) {
	vdirent_t *dirent;

	VN_ASSERT_LOCK_WR(node);
	dirent = vdirent_cache_lookup(node, name, namelen);
	if(dirent) {
		vdirent_cache_rem(node, dirent);
	}
}

static bool vdirent_cache_reclaim(void) {
	vdirent_t *dent;
	size_t hash;

	wrlocked(&vd_lock) {
		dent = locklist_pop_front(&vd_lru);

		/*
		 * There is no dirent, which can be freed.
		 */
		if(dent == NULL) {
			return false;
		}

		/*
		 * Remove the dirent from the hashtable.
		 */
		hash = vdirent_cache_hash(dent->owner, dent->name);
		hashtab_remove(&vd_ht, hash, &dent->node);
	}

	/*
	 * It's safe to free the dirent now.
	 */
	vdirent_free(dent);
	return true;
}
vm_reclaim("vdirent-cache", vdirent_cache_reclaim);

static bool vnode_cache_reclaim(void) {
	vnode_t *node = NULL;
	size_t hash;

	/*
	 * Choose a node from the lru.
	 */
	node = locklist_first(&vn_lru);

	/*
	 * There is no node, which can be freed.
	 */
	if(node == NULL) {
		return false;
	}

	assert(!vnode_flags_test(node, VN_DIRTY));

	/*
	 * Cannot free the vnode if it still has some dirty buffers.
	 */
	synchronized(&node->object.lock) {
		if(node->dirty != 0) {
			vnode_sched_sync_pages(node);
			return false;
		}
	}

	/*
	 * Calculate the hash.
	 */
	hash = vnode_cache_hash(node->fs, node->ino);

	/*
	 * The filesystem needs to be locked for vput callback.
	 */
	sync_scope_acquire(&node->fs->lock);
	synchronized(&node->object.lock) {
		if(node->dirty != 0) {
			/*
			 * TODO can unlock fs->lock again before calling
			 * that function.
			 */
			vnode_sched_sync_pages(node);
			return false;
		}

		sync_scope_acquire(&vn_lru.lock);

		/*
		 * If the node is no longer on the LRU then the node is being
		 * used by someone and thus cannot be freed. Remember that
		 * pageout might reference the vnode without removing it
		 * from the lru. This case can be ignored, because
		 * vnode_destroy() (which will be eventually called by
		 * filesys_vput) will remove every page of the vnode from
		 * pageout and thus pageout cannot have any reference to the
		 * vnode when freeng it.
		 */
		if(vnode_flags_test(node, VN_VCLRU) == false) {
			return false;
		} else {
			list_remove(&vn_lru.list, &node->lru_node);
		}
	}

	/*
	 * There are no references to this vnode and there can't be any more
	 * references (except pageout, but that was explained above).
	 * Furthermore every buffered page was written to disk.
	 * It's safe to free the vnode now.
	 */
	hashtab_remove(&vn_ht, hash, &node->node);
	filesys_vput(node->fs, node);

	return true;
}
vm_reclaim("vnode-cache", vnode_cache_reclaim);

void __init vcache_init(void) {
	hashtab_alloc(&vn_ht, VCACHE_MEMORY / sizeof(list_t), VM_WAIT);
	hashtab_alloc(&vd_ht, VCACHE_MEMORY / sizeof(list_t), VM_WAIT);
}

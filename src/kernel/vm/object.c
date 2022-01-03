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
#include <vm/object.h>
#include <vm/slab.h>
#include <vm/pghash.h>
#include <vm/page.h>
#include <vm/phys.h>
#include <vm/pageout.h>
#include <vm/pager.h>
#include <vm/vas.h>

static void vm_object_page_free(vm_object_t *object, vm_page_t *page);
static vm_pager_t dummy_pager = {
	/*
	 * TODO XXX Can be removed once swap is finished.
	 */
	.flags = VM_PAGER_PGHASH,
};

static DEFINE_VM_SLAB(vm_object_slab, sizeof(vm_object_t), 0);

void vm_object_init(vm_object_t *object, vm_objoff_t size, vm_obj_ops_t *ops,
	vm_pager_t *pager)
{
	list_init(&object->pages);
	sync_init(&object->lock, SYNC_MUTEX);
	ref_init(&object->ref);
	object->ops = ops;
	object->size = size;
	if(pager) {
		object->pager = pager;
	} else {
		/*
		 * TODO swap pager
		 */
		object->pager = &dummy_pager;
	}

	if(!VM_IS_SHDW(object)) {
		list_init(&object->maps);
	}
}

void vm_object_destroy(vm_object_t *object) {
	list_destroy(&object->pages);
	sync_destroy(&object->lock);
}

vm_object_t *vm_object_alloc(vm_objoff_t size, vm_obj_ops_t *ops) {
	vm_object_t *object;

	object = vm_slab_alloc(&vm_object_slab, VM_WAIT);
	vm_object_init(object, size, ops, NULL);

	return object;
}

void vm_object_free(vm_object_t *object) {
	vm_object_destroy(object);
	vm_slab_free(&vm_object_slab, object);
}

void vm_object_clear(vm_object_t *object) {
	vm_page_t *page;

	sync_assert(&object->lock);
	kassert(VM_IS_DEAD(object), "[vm] object: clearing an alive object");

	/*
	 * Free any pages associated with this object. It is the task of the
	 * destroy-callback to write pages to disk if necessary (i.e. sync
	 * the cached pages of vnodes). No syncing happens here.
	 */
	foreach(page, &object->pages) {
		/*
		 * Pageout might currently be tampering with the page, so
		 * let's prevent pageout from using the page anymore.
		 */
		vm_pageout_rem(object, page);

		vm_page_clean(page);
		vm_object_page_remove(object, page);
		vm_page_free(page);
	}
}

vm_page_t *vm_object_page_alloc(vm_object_t *object, vm_objoff_t off) {
	vm_page_t *page;

	sync_assert(&object->lock);
	page = vm_page_alloc(VM_NOFLAG);
	if(page) {
		vm_pghash_add(object, VM_PGHASH_PAGE, off, &page->node);

		vm_page_busy(page);
		vm_page_pin(page);
		list_node_init(page, &page->obj_node);
		list_node_init(page, &page->pgout_node);
		list_append(&object->pages, &page->obj_node);

		/*
		 * TODO temporary. Currently only vnodes are capable of
		 * pageout.
		 */
		if(VM_IS_VNODE(object)) {
			vm_pageout_add(page);
		}
	}

	return page;
}

void vm_object_page_remove(vm_object_t *object, vm_page_t *page) {
	kassert(vm_page_object(page) == object, NULL);
	sync_assert(&object->lock);

	vm_pghash_rem(object, &page->node);
	list_remove(&object->pages, &page->obj_node);
	list_node_destroy(&page->obj_node);
	list_node_destroy(&page->pgout_node);
}

static void vm_object_page_free(vm_object_t *object, vm_page_t *page) {
	kassert(vm_page_object(page) == object, NULL);
	sync_assert(&object->lock);

	vm_object_page_remove(object, page);
	vm_page_pin(page);
	vm_page_flag_set(page, VM_PG_DEALLOC);
	vm_page_unpin(page);
}

void vm_object_pages_migrate(vm_object_t *dst, vm_object_t *src,
	vm_objoff_t off)
{
	vm_page_t *page;

	sync_assert(&dst->lock);
	sync_assert(&src->lock);

retry:
	foreach(page, &src->pages) {
		if(vm_page_offset(page) < off) {
			continue;
		}

		if(vm_page_is_busy(page)) {
			/*
			 * Either the page is currenty being initialized
			 * or pageout is using the page.
			 */
			vm_page_pin(page);

			sync_release(&src->lock);
			sync_release(&dst->lock);

			vm_page_busy_wait(page);
			vm_page_unpin(page);

			/*
			 * The order of locking is important here, because
			 * we might be migrating pages from a shadow object,
			 * to a shadow of this object.
			 * This means that _dst_ is deeper in the shadow chain
			 * than _src_. Since the order of locking in shadow
			 * chain goes from the deepest shadow objects to the
			 * shadow root, _dst_ has to be locked first.
			 */
			sync_acquire(&dst->lock);
			sync_acquire(&src->lock);

			/*
			 * Anything might have happened while unlocking
			 * the objects, so we have to start all over again.
			 */
			goto retry;
		}

		if(vm_pghash_lookup(dst, vm_page_offset(page))) {
			/*
			 * Free the page since the destination already has
			 * a page at the offset.
			 */
			vm_pageout_rem(src, page);
			vm_page_clean(page);
			vm_object_page_free(src, page);
		} else {
			vm_page_lock(page);
			vm_pghash_migrate(src, &page->node, dst);
			list_remove(&src->pages, &page->obj_node);
			list_append(&dst->pages, &page->obj_node);
			vm_page_unlock(page);
		}
	}
}

void vm_object_resize(vm_object_t *object, vm_objoff_t size) {
	vm_objoff_t old, last, offset;
	vm_page_t *page;

	sync_assert(&object->lock);

	/*
	 * Shadow objects never resize. The only object that currently
	 * resize are vnode objects.
	 */
	kassert(object == vm_shadow_root(object),
		"[vm] object: resizing a shadow object");

	old = object->size;
	object->size = size;

	/*
	 * Nothing has to be unmapped, if the size does not change
	 * or increases.
	 */
	if(size >= old) {
		return;
	}

	last = object->size & PAGE_MASK;

	/*
	 * TODO free any swapspace after the new object->size.
	 */

	/*
	 * Unmap and free every page that is no longer valid.
	 */
again:
	foreach(page, &object->pages) {
		offset = vm_page_offset(page);

		if(offset < (size & PAGE_MASK)) {
			continue;
		} else if(vm_page_is_busy(page)) {
			vm_page_pin(page);
			sync_release(&object->lock);
			vm_page_busy_wait(page);
			vm_page_unpin(page);
			sync_acquire(&object->lock);
			goto again;
		}

		/*
		 * Unmap the page from every address space mapping
		 * this object.
		 */
		vm_page_unmap(object, page);

		if(!ALIGNED(size, PAGE_SZ) && offset == last) {
			size_t pgoff = object->size & ~PAGE_MASK;
			vm_page_zero_range(page, pgoff, PAGE_SZ - pgoff);
		} else {
			vm_pageout_rem(object, page);
			vm_page_clean(page);
			vm_object_page_free(object, page);
		}
	}
}

void vm_object_page_error(vm_object_t *object, vm_page_t *page) {
	/*
	 * This type of error can only happen while filling the page
	 * with data (e.g. I/O error). Thus the page has to be busy.
	 */
	vm_page_assert_busy(page);
	vm_page_assert_pinned(page);

	synchronized(&object->lock) {
		vm_page_error(page);
		vm_pghash_rem(object, &page->node);
	}

	vm_page_unpin(page);
}

static bool vm_object_page_wait(vm_object_t *object, vm_page_t *page) {
	bool success;

	vm_page_assert_pinned(page);
	sync_assert(&object->lock);
	kassert(vm_page_object(page) == object,
		"[vm] object: object/page mismatch");

	/*
	 * The page might not be initialized yet or currently being
	 * written to disk.
	 */
	while(vm_page_is_busy(page)) {
		sync_release(&object->lock);
		success = vm_page_busy_wait(page);
		sync_acquire(&object->lock);
		if(!success) {
			/*
			 * Initializing the page failed due to an I/O
			 * error, thus the page is no longer valid.
			 */
			return false;
		}
	}

	return true;
}

int vm_object_page_resident(vm_object_t *object, vm_objoff_t off,
	vm_page_t **pagep)
{
	vm_pghash_node_t *node;
	vm_page_t *page;
	int err;

	kassert(ALIGNED(off, PAGE_SZ), "[vm] object: unaligned object "
		"offset: 0x%llx", off);
	sync_assert(&object->lock);

	for(;;) {
		if(off >= object->size) {
			return -ERANGE;
		}

		node = vm_pghash_lookup(object, off);
		if(node != NULL && vm_pghash_type(node) == VM_PGHASH_PAGE) {
			/*
			 * The object contained an in memory page at the
			 * address, which is very convenient.
			 */
			page = PGH2PAGE(node);
			vm_page_pin(page);

			/*
			 * The page might not be initialized yet (somebody else
			 * is currently initializing the page).
			 */
			if(vm_object_page_wait(object, page) == false) {
				/*
				 * Retry in case of an error while initializing
				 * the page. (Unpinning the page might actually
				 * free it)
				 */
				vm_page_unpin(page);
				continue;
			}
		} else {
			/*
			 * Try reading the page from disk.
			 */
			err = vm_pager_pagein(object, off, node, &page);
			if(err) {
				return err;
			}
		}

		return *pagep = page, 0;
	}

	notreached();
}

int vm_generic_fault(vm_object_t *object, vm_objoff_t off,
	__unused vm_flags_t flags, __unused vm_flags_t *map_flags,
	vm_page_t **pagep)
{
	vm_page_t *page;
	int err;

	/*
	 * Allocate a new page. vm_object_page_alloc returns the
	 * page in a pinned and busy state.
	 */
	page = vm_object_page_alloc(object, off);
	if(page == NULL) {
		return  -ENOMEM;
	}

	vm_page_dirty(page);
	sync_release(&object->lock);
	err = object->ops->initpage(object, page);
	if(!err) {
		sync_acquire(&object->lock);
		vm_page_unbusy(page);
	} else {
		vm_object_page_error(object, page);
		sync_acquire(&object->lock);
	}

	return *pagep = page, err;
}

int vm_object_fault(vm_object_t *object, vm_objoff_t off, vm_flags_t access,
	vm_flags_t *map_flags, vm_page_t **pagep)
{
	vm_page_t *page;
	int err;

	kassert(ALIGNED(off, PAGE_SZ), "[vm] object: unaligned object "
		"offset: 0x%llx", off);
	sync_assert(&object->lock);

	err = vm_object_page_resident(object, off, &page);
	if(err == -ENOENT) {
		err = object->ops->fault(object, off, access, map_flags,
			&page);
	}

	if(!err) {
		vm_page_assert_pinned(page);
		vm_page_assert_not_busy(page);

		if(VM_PROT_WR_P(*map_flags)) {
			vm_page_dirty(page);
		}
	}

	return *pagep = page, err;
}

void vm_object_map_add(vm_object_t *object, vm_map_t *map) {
	object = vm_shadow_root(object);
	sync_scope_acquire(&object->lock);
	list_append(&object->maps, &map->obj_node);
}

void vm_object_map_rem(vm_object_t *object, vm_map_t *map) {
	object = vm_shadow_root(object);
	sync_scope_acquire(&object->lock);
	list_remove(&object->maps, &map->obj_node);
}

int vm_object_map(vm_object_t *object, vm_map_t *map) {
	assert(object == vm_shadow_root(object));

	if(object->ops->map) {
		return object->ops->map(object, map);
	} else {
		vm_object_map_add(object, map);
		return 0;
	}
}

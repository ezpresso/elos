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
#include <kern/futex.h>
#include <vm/page.h>
#include <vm/object.h>
#include <vm/pageout.h>
#include <vm/phys.h>
#include <vm/kern.h>
#include <vm/vas.h>
#include <vm/mmu.h>
#include <sys/limits.h>
#include <lib/string.h>
#include <config.h>

void vm_page_zero_range(vm_page_t *page, size_t off, size_t size) {
	vm_paddr_t phys = vm_page_phys(page);
	void *ptr;

	kassert(off + size <= PAGE_SZ, "[vm] page: range not inside page: "
		" offset: %d size: %d", off, size);

	ptr = vm_kern_map_quick(phys);
	memset(ptr + off, 0x0, size);
	vm_kern_unmap_quick(ptr);
}

void vm_page_zero(vm_page_t *page) {
	return vm_page_zero_range(page, 0, PAGE_SZ);
}

void vm_page_dirty(vm_page_t *page) {
	vm_object_t *object = vm_page_object(page);

	sync_assert(&vm_page_object(page)->lock);
	vm_page_flag_set(page, VM_PG_DIRTY);
	if(object->ops->dirty) {
		object->ops->dirty(object, page);
	}
}

void vm_page_pin(vm_page_t *page) {
	uint16_t pincnt;

	/*
	 * The i386 page directory entry pages use the pin-count
	 * for keeping track of the valid page table entries.
	 * TODO always associate a page with an object (i.e. kern_object?)
	 */
#if CONFIGURED(INVARIANTS)
	if(vm_page_object(page)) {
		sync_assert(&vm_page_object(page)->lock);
	}
#endif

	pincnt = atomic_inc_relaxed(&page->pincnt);
	kassert(pincnt < UINT16_MAX, "[vm] page: pin count overflow");
	if(vm_page_pageout_p(page) && pincnt == 0) {
		vm_pageout_pin(page);
	}
}

uint16_t vm_page_unpin(vm_page_t *page) {
	uint16_t pincnt;

	pincnt = atomic_dec_relaxed(&page->pincnt);
	kassert(pincnt > 0, "[vm] page: pin count underflow");
	if(pincnt == 1) {
		if(vm_page_flag_test(page, VM_PG_DEALLOC)) {
			vm_page_flag_clear(page, VM_PG_DEALLOC);
			vm_page_free(page);
		} else if(vm_page_flag_test(page, VM_PG_ERR)) {
			vm_page_assert_busy(page);

			/*
			 * Technically this is not needed, but vm_page_free
			 * excpects the page flags to be zero for debugging
			 * purposes.
			 */
			vm_page_flag_clear(page, VM_PG_BUSY | VM_PG_ERR);
			vm_page_free(page);
		} else if(vm_page_pageout_p(page)) {
			vm_pageout_unpin(page);
		}
	}

	return pincnt;
}

void vm_page_busy(vm_page_t *page) {
	__unused vm_pgflags_t flags;

	kassert(vm_page_object(page), NULL);
	sync_assert(&vm_page_object(page)->lock);

	flags = vm_page_flag_set(page, VM_PG_BUSY);
	kassert(!(flags & VM_PG_BUSY), "[vm] page: double busy");
}

void vm_page_unbusy(vm_page_t *page) {
	__unused vm_pgflags_t flags;

	flags = vm_page_flag_clear(page, VM_PG_BUSY);
	kassert(flags & VM_PG_BUSY, "[vm] page: page was not busy");
	kassert(!(flags & VM_PG_ERR), "[vm] page: unbusy erroneous page");

	/*
	 * Wakeup threads waiting for this page.
	 */
	kern_wake(&page->flags, INT_MAX, 0);
}

void vm_page_error(vm_page_t *page) {
	vm_page_flag_set(page, VM_PG_ERR);

	/*
	 * Wakeup threads waiting for this page.
	 */
	kern_wake(&page->flags, INT_MAX, 0);
}

bool vm_page_busy_wait(vm_page_t *page) {
	vm_pgflags_t flags;

	vm_page_assert_pinned(page);
	while(((flags = atomic_load_relaxed(&page->flags)) & (VM_PG_BUSY |
		VM_PG_ERR)) == VM_PG_BUSY)
	{
		/* TODO interruptable? */
		kern_wait(&page->flags, flags, 0);
	}

	if(flags & VM_PG_ERR) {
		return false;
	} else {
		return true;
	}
}

bool vm_page_trylock(vm_page_t *page) {
	return !(vm_page_flag_set(page, VM_PG_LOCKED) & VM_PG_LOCKED);
}

void vm_page_lock(vm_page_t *page) {
	vm_pgflags_t flags;

	while((flags = vm_page_flag_set(page, VM_PG_LOCKED)) & VM_PG_LOCKED) {
		kern_wait(&page->flags, flags, 0);
	}
}

void vm_page_unlock(vm_page_t *page) {
	kassert(F_ISSET(vm_page_flags(page), VM_PG_LOCKED), "[vm] page: "
		"unlock: page not locked");

	vm_page_flag_clear(page, VM_PG_LOCKED);
	kern_wake(&page->flags, INT_MAX, 0);
}

bool vm_page_lock_wait(vm_page_t *page) {
	vm_pgflags_t flags = vm_page_flags(page);
	if(flags & VM_PG_LOCKED) {
		kern_wait(&page->flags, flags, 0);
		return true;
	} else {
		return false;
	}
}

void vm_page_unmap(vm_object_t *object, vm_page_t *page) {
	vm_objoff_t offset = vm_page_offset(page);
	vm_vaddr_t addr;
	vm_map_t *map;

	kassert(object == vm_shadow_root(object), NULL);
	sync_assert(&object->lock);

	foreach(map, &object->maps) {
		sync_scope_acquire(&map->lock);

		/*
		 * Don't unmap anything if the mapping does not touch the
		 * segment of object that is being removed.
		 */
		if(offset >= map->offset && offset < map->offset +
			vm_map_size(map))
		{
			addr = vm_map_offset_addr(map, offset);

			/*
			 * Remember that the map might belong to
			 * the cur vas and that another page might
			 * actually be present in the mapping.
			 */
			mmu_unmap_page(&map->vas->mmu, addr, page);
		}
	}
}

vm_object_t *vm_page_lock_object(vm_page_t *page) {
	vm_object_t *object;

again:
	vm_page_lock(page);
	object = vm_page_object(page);

	/*
	 * We cannot sleep while waiting for the object lock, because we
	 * might risk a deadlock (object has to be locked before the page).
	 * However we might be lucky and able to acquire the lock without
	 * sleeping. If this fails we have to do it the hard way...
	 */
	if(sync_trylock(&object->lock) == false) {
		/*
		 * We have to unlock the page beforehand to avoid a
		 * deadlock. To make sure the object stays with us
		 * we add a reference.
		 */
		vm_object_ref(object);
		vm_page_unlock(page);
		sync_acquire(&object->lock);

		/*
		 * The object of the page might have changed while waiting
		 * for the object lock (see vm_object_pages_migrate). Thus
		 * we have to recheck if the correct object was locked. If
		 * the page was moved to another object the whole procedure
		 * needs to be retried.
		 */
		vm_page_lock(page);
		if(vm_page_object(page) != object) {
			vm_page_unlock(page);
			sync_release(&object->lock);
			vm_object_unref(object);
			goto again;
		} else {
			vm_object_unref(object);
		}
	}

	vm_page_unlock(page);
	return object;
}

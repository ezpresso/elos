/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 *
 * Copyright (c) 2018, Elias Zell
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
#include <kern/symbol.h>
#include <vm/vm.h>
#include <vm/malloc.h>
#include <vm/slab.h>
#include <vm/vmem.h>
#include <vm/page.h>
#include <vm/mmu.h> /* vtopage */

#define KM_MIN_ORDER	VM_PTR_ALING_LOG
#define KM_MAX_ORDER	PAGE_SHIFT
#define KM_NUM_ORDER 	(KM_MAX_ORDER - KM_MIN_ORDER)
#define KM_ORDER(size) \
	((sizeof(unsigned int) * 8 - __builtin_clz((size) - 1)) - KM_MIN_ORDER)
#define KM_SLAB_MAX	(1U << (PAGE_SHIFT - 1))

static vm_slaballoc_t vm_heap_slabs[KM_NUM_ORDER];

void *kmalloc(size_t size, vm_flags_t flags) {
	void *alloc;

	VM_INIT_ASSERT(VM_INIT_KMALLOC);
	VM_FLAGS_CHECK(flags, VM_WAIT | VM_ZERO);

	if(size > KM_SLAB_MAX) {
		vm_page_t *page;

		size = ALIGN(size, PAGE_SZ);

		/*
		 * Big allocations are allocated using the vmem
		 * interface.
		 */
		alloc = vmem_alloc_backed(size, flags);
		if(alloc == VMEM_ERR_PTR) {
			return NULL;
		}

		/*
		 * Some info needs to be stored in the page. This does not use
		 * any unnecessary memory since page->malloc_sz is inside a
		 * union in the vm_page_t.
		 */
		page = vtopage(alloc);
		kassert(vm_page_state(page) == VM_PG_NORMAL, "[vm] kmalloc: "
			"invalid page state: %d", vm_page_state(page));

		/*
		 * Set the type of the page to "kmalloc big allocation" and
		 * store the size of the allocation in the page, so that kfree
		 * knows the size and that it should call vmem_free_backed
		 * instead of vm_slab_free.
		 */
		vm_page_set_state(page, VM_PG_MALLOC);
		page->malloc_sz = size;
	} else {
		/*
		 * Small allocations are allocated using pow2 slab allocators.
		 */
		size = max(size, (1U << KM_MIN_ORDER));
		alloc = vm_slab_alloc(&vm_heap_slabs[KM_ORDER(size)], flags);
	}

#if notyet
	if(alloc && still some space in allocation) {
		asan_protect();
	}
#endif

	return alloc;
}
export(kmalloc);

void kfree(void *ptr) {
	vm_page_t *page;
	vm_pgstate_t state;

	VM_INIT_ASSERT(VM_INIT_KMALLOC);
	kassert(ptr != NULL, "[vm] kfree: freeing NULL pointer");

#if 0
	kprintf("[vm] kfree: freeing 0x%p\n", ptr);
#endif

	page = vtopage(ptr);
	state = vm_page_state(page);

	if(state == VM_PG_NORMAL) {
		kpanic("[vm] kfree: page has type \"NORMAL\"");
	} else if(state == VM_PG_SLAB) {
		vm_slab_free(vm_slab_get_alloc(page->slab), ptr);
	} else {
		kassert(state == VM_PG_MALLOC, "[vm] kfree: invalid page "
			" state: %d", state);
		vmem_free_backed(ptr, page->malloc_sz);
	}
}
export(kfree);

void __init vm_malloc_init(void) {
	kprintf("[vm] malloc: initializing\n");

	for(size_t i = 0; i < KM_NUM_ORDER; i++) {
		vm_slab_create(&vm_heap_slabs[i], "malloc slab",
			(1 << (i + KM_MIN_ORDER)), 0);
	}

	vm_init_done(VM_INIT_KMALLOC);
}

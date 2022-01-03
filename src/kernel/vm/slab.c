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
#include <lib/string.h>
#include <vm/slab.h>
#include <vm/vmem.h>
#include <vm/page.h>
#include <vm/phys.h>
#include <vm/vm.h>
#include <vm/mmu.h> /* vtopage */
#include <vm/pressure.h>
#include <vm/reclaim.h>
#include <compiler/asan.h>
#include <config.h>

typedef struct vm_freeobj {
	struct vm_freeobj *next;
} vm_freeobj_t;

typedef struct vm_slab {
	vm_slaballoc_t *alloc;
	list_node_t node; /* node for alloc->free */
	struct vm_freeobj *free;
	void *ptr;
	size_t nfree;
	size_t nobj;
} vm_slab_t;

static DEFINE_VM_SLAB(vm_slabs, sizeof(vm_slab_t), 0);

/**
 * @brief A list of every slab allocator in the system.
 */
static DEFINE_LIST(vm_slab_list);
static sync_t vm_slab_lock = SYNC_INIT(MUTEX);

static void vm_slab_alloc_add(vm_slaballoc_t *alloc) {
	list_node_init(alloc, &alloc->node);

	sync_scope_acquire(&vm_slab_lock);
	list_append(&vm_slab_list, &alloc->node);
}

static void vm_slab_alloc_rem(vm_slaballoc_t *alloc) {
	synchronized(&vm_slab_lock) {
		list_remove(&vm_slab_list, &alloc->node);
	}

	list_node_destroy(&alloc->node);
}

void vm_slab_create(vm_slaballoc_t *alloc, const char *name, size_t size,
	size_t align)
{
	alloc->name = name;
	alloc->obj_size = size;
	alloc->align = align;
	sync_init(&alloc->lock, SYNC_MUTEX);
	list_init(&alloc->free);
	vm_slab_alloc_add(alloc);
}

void vm_slab_destroy(vm_slaballoc_t *alloc) {
	vm_slab_alloc_rem(alloc);
	sync_destroy(&alloc->lock);
	list_destroy(&alloc->free);
}

vm_slaballoc_t *vm_slab_get_alloc(vm_slab_t *slab) {
	return slab->alloc;
}

/**
 * @brief Add a free object to a slab's free list.
 */
static inline void vm_slab_add_free(vm_slab_t *slab, void *ptr) {
	vm_freeobj_t *obj;

	kassert(ptr >= slab->ptr, NULL);

	/*
	 * If the memory is not allocated, it can be used for storing
	 * the vm_freeobj_t structure.
	 */
	kassert(slab->alloc->obj_size >= sizeof(vm_freeobj_t), NULL);
	obj = ptr;

	obj->next = slab->free;
	slab->free = obj;
	slab->nfree++;
}

/**
 * @brief Satisfy the alignment requirements of an allocators.
 */
static inline void *vm_slab_mem_align(vm_slaballoc_t *alloc, void *ptr) {
	return ALIGN_PTR(ptr, max(alloc->align, (size_t)VM_PTR_ALIGN));
}

/**
 * @brief Initialize a new vm_slab with the memory provided by @p ptr.
 */
static void vm_slab_init_slab(vm_slaballoc_t *alloc, vm_slab_t *slab, void *ptr,
	size_t size)
{
	void *end = ptr + size;

	sync_assert(&alloc->lock);

	/*
	 * Initialize the slab structure.
	 */
	list_node_init(slab, &slab->node);
	slab->alloc = alloc;
	slab->free = NULL;
	slab->nfree = 0;
	slab->ptr = ptr;

	/*
	 * nobj will be incremented in the loop below.
	 */
	slab->nobj = 0;

	/*
	 * vm_slab_free needs to know, to which slab a pointer belongs.
	 * This information is stored in the vm_page-structures of the
	 * underlying physical memory. Using this, no memory is wasted
	 * by having something like an object-header containing this
	 * info.
	 */
	for(size_t i = 0; i < size; i += PAGE_SZ) {
		vm_page_t *page = vtopage(ptr + i);

		kassert(vm_page_state(page) == VM_PG_NORMAL, NULL);
		vm_page_set_state(page, VM_PG_SLAB);
		page->slab = slab;
	}

	/*
	 * Make sure that the first object is properly aligned.
	 */
	ptr = vm_slab_mem_align(alloc, ptr);

	/*
	 * Add every object in the slab to the free-list
	 * of the slab.
	 */
	while((ptr + alloc->obj_size) <= end) {
		vm_slab_add_free(slab, ptr);
		slab->nobj++;

		/*
		 * Calculate the address of the next object.
		 */
		ptr = vm_slab_mem_align(alloc, ptr + alloc->obj_size);
	}

	asan_prot(slab->ptr, size);
}

/**
 * @brief Add a slab full of vm_slab structures to the slab cache.
 */
static void vm_add_slab_structs(vm_slab_t *slab, size_t size) {
	kassert(size >= PAGE_SZ, NULL);

	sync_scope_acquire(&vm_slabs.lock);

	/*
	 * The first vm_slab is used as the slab structure for the allocator
	 * and the rest of the slabs in the memory region can be allocated
	 * using vm_alloc_slab_struct.
	 */
	vm_slab_init_slab(&vm_slabs, slab, &slab[1], size - sizeof(vm_slab_t));
	list_append(&vm_slabs.free, &slab->node);
}

/**
 * @brief Allocate a new vm_slab structure.
 * @param flags VM_WAIT or 0
 */
static vm_slab_t *vm_alloc_slab_struct(void) {
	vm_slab_t *slab;

	/*
	 * The VM_SLAB_NOVALLOC flag prevents vm_slab_alloc from calling
	 * vm_alloc_slab_struct, which would result in an infinite recursion
	 * (until stack overflow of course).
	 */
	while((slab = vm_slab_alloc(&vm_slabs, VM_SLAB_NOVALLOC)) == NULL) {
		slab = vmem_alloc_backed(PAGE_SZ, VM_NOWAIT);
		if(slab == VMEM_ERR_PTR) {
			return NULL;
		}

		vm_add_slab_structs(slab, PAGE_SZ);
	}

	return slab;
}

static vm_slab_t *vm_slab_get(vm_slaballoc_t *alloc, vm_flags_t flags) {
	vm_slab_t *slab = NULL, *tmp;
	void *mem;

	sync_assert(&alloc->lock);
	VM_FLAGS_CHECK(flags, VM_WAIT | VM_SLAB_NOVALLOC);

retry_slab:
	/*
	 * First check if there is a slab available which still
	 * has some space left.
	 */
	if((slab = list_first(&alloc->free)) != NULL ||
		F_ISSET(flags, VM_SLAB_NOVALLOC))
	{
		return slab;
	}

	/*
	 * We have to allocate a new slab.
	 */
	slab = vm_alloc_slab_struct();
	if(slab == NULL) {
		if(!F_ISSET(flags, VM_WAIT)) {
			return NULL;
		} else {
			sync_release(&alloc->lock);

			/*
			 * The slab struct allocator is out of slabs for
			 * storing slab structs, so we have to wait for
			 * at least 1 page (physical + virtual) to become
			 * available.
			 */
			vm_mem_wait(VM_PR_MEM_KERN, PAGE_SZ);
			vm_mem_wait(VM_PR_MEM_PHYS, PAGE_SZ);
			sync_acquire(&alloc->lock);
			goto retry_slab;
		}
	}

	/*
	 * Now that we have a slab struct we need to associate one
	 * page of free memory with the slab for the objects inside
	 * the slab.
	 */
retry_mem:
	mem = vmem_alloc_backed(PAGE_SZ, VM_NOWAIT);
	if(mem == NULL) {
		if(!F_ISSET(flags, VM_WAIT)) {
			vm_slab_free(&vm_slabs, slab);
			return NULL;
		} else {
			sync_release(&alloc->lock);
			vm_mem_wait(VM_PR_MEM_KERN, PAGE_SZ);
			vm_mem_wait(VM_PR_MEM_PHYS, PAGE_SZ);
			sync_acquire(&alloc->lock);

			/*
			 * Recheck if somebody else was faster than us and
			 * allocated memory while we were sleeping.
			 */
			if((tmp = list_first(&alloc->free)) != NULL) {
				vm_slab_free(&vm_slabs, slab);
				return tmp;
			} else {
				goto retry_mem;
			}
		}
	}

	vm_slab_init_slab(alloc, slab, mem, PAGE_SZ);

	/*
	 * Add the slab to the front of the
	 * list (even tough empty slabs are
	 * inserted at the end), because
	 * one elemt is removed anyway.
	 */
	list_add(&alloc->free, &slab->node);
	return slab;
}

void vm_slab_add_mem(vm_slaballoc_t *alloc, void *ptr, size_t size) {
	vm_slab_t *slab;

	kassert(ALIGNED(size, PAGE_SZ), NULL);
	kassert(PTR_ALIGNED(ptr, PAGE_SZ), NULL);
	kassert(size > 0, NULL);

	/*
	 * Use the memory provided by the caller for allocating a new slab
	 * structure, if no free vm_slab_t is cached.
	 */
	while((slab = vm_slab_alloc(&vm_slabs, VM_SLAB_NOVALLOC)) == NULL) {
		vm_add_slab_structs(ptr, PAGE_SZ);
		ptr += PAGE_SZ;
		size -= PAGE_SZ;

		/*
		 * Used all of the memory for trying to allocate vm_slab_t s.
		 * No furhter memory available to use for the allocator.
		 */
		if(size == 0) {
			return;
		}
	}

	/*
	 * Insert the new slab into the allocator's free-list.
	 */
	sync_scope_acquire(&alloc->lock);
	vm_slab_init_slab(alloc, slab, ptr, size);
	list_append(&alloc->free, &slab->node);
}

void *vm_slab_alloc(vm_slaballoc_t *alloc, vm_flags_t flags) {
	void *ptr = NULL;

	VM_FLAGS_CHECK(flags, VM_WAIT | VM_ZERO | VM_SLAB_NOVALLOC);
	synchronized(&alloc->lock) {
		vm_slab_t *slab;
		vm_freeobj_t *obj;

		slab = vm_slab_get(alloc, flags & ~VM_ZERO);
		if(slab == NULL) {
			return slab;
		}

		/*
		 * Pop an object off the free-list.
		 */
		kassert(slab->free, NULL);
		obj = slab->free;
		asan_rmprot(obj, alloc->obj_size);

		slab->free = obj->next;

		/*
		 * Remove the slab from the cache's free-list if
		 * necessary.
		 */
		if(--slab->nfree == 0) {
			list_remove(&alloc->free, &slab->node);
		}

		ptr = obj;
	}

	/*
	 * Initialize the memory if needed.
	 */
	if(flags & VM_ZERO) {
		memset(ptr, 0x00, alloc->obj_size);
	}
#if CONFIGURED(DEVELOPMENT)
	else {
		/*
		 * Poison the memory to detect some problems
		 * where memory is not initialized.
		 */
		memset(ptr, POSION_ALLOC, alloc->obj_size);
	}
#endif

	return ptr;
}

/**
 * TODO @p alloc is redundant
 */
void vm_slab_free(vm_slaballoc_t *alloc, void *ptr) {
	vm_slab_t *slab;
	vm_page_t *page;

	kassert(ptr != NULL, "[vm] slab: freeing NULL pointer");

#if CONFIGURED(DEVELOPMENT)
	/*
	 * Poison the memory to make it more difficult to use the pointer
	 * afterwards without noticing. Furhtermore the memset will trigger
	 * ASAN if somebody forgot list_node_destroy, list_destroy, etc.
	 */
	memset(ptr, POISON_FREE, alloc->obj_size);
#endif

	/*
	 * Lookup the vm_slab the pointer belongs to.
	 */
	page = vtopage(ptr);
	kassert(vm_page_state(page) == VM_PG_SLAB, "[vm] slab: freeing "
		"\"non-slab\" memory (page state: %d)", vm_page_state(page));

	slab = page->slab;
	kassert(slab->alloc == alloc, "[vm] slab: freeing memory on the wrong "
		"allocator");

	sync_acquire(&alloc->lock);
	vm_slab_add_free(slab, ptr);
	asan_prot(ptr, alloc->obj_size);

	/*
	 * The slab is now completely empty, so we could technically
	 * free the page of the slab. However we still leave the
	 * page inside the slab to speed up subsequent allocations.
	 * If memory pressure is too high, the page can still be freed
	 * by the reclaim thread.
	 */
	if(slab->nfree == 1) {
		/*
		 * If nfree changed from 0 to 1, add this
		 * slab back on the allocators's free-list.
		 */
		list_add(&alloc->free, &slab->node);
	}

	sync_release(&alloc->lock);
}

static bool vm_slab_reclaim(void) {
	vm_slaballoc_t *alloc;
	vm_slab_t *slab;

	sync_acquire(&vm_slab_lock);
	foreach(alloc, &vm_slab_list) {
		sync_acquire(&alloc->lock);

		/*
		 * The empty slabs are always at the
		 * end of the free-list.
		 */
		slab = list_last(&alloc->free);
		if(slab && slab->nfree == slab->nobj) {
			list_remove(&alloc->free, &slab->node);
			sync_release(&alloc->lock);
			sync_release(&vm_slab_lock);

			list_node_destroy(&slab->node);
			if(alloc == &vm_slabs) {
				/*
				 * In the vm_slabs cache the the slab structure
				 * and the allocatable slabs reside inside one
				 * allocation.
				 */
				vmem_free_backed(slab, PAGE_SZ);
			} else {
				asan_rmprot(slab->ptr, PAGE_SZ);
				vmem_free_backed(slab->ptr, PAGE_SZ);
				vm_slab_free(&vm_slabs, slab);
			}

			return true;
		} else {
			sync_release(&alloc->lock);
		}
	}

	sync_release(&vm_slab_lock);
	return false;
}
vm_reclaim("slab-cache", vm_slab_reclaim);

void vm_slab_init(void) {
	vm_slaballoc_t *alloc;

	/*
	 * Add every statically allocated slab allocator to the
	 * global allocator list.
	 */
	section_foreach(alloc, VM_SLAB_SECTION) {
		vm_slab_alloc_add(alloc);
	}
}

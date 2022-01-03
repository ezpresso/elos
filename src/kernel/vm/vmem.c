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
#include <kern/atomic.h>
#include <kern/sync.h>
#include <compiler/asan.h>
#include <lib/list.h>
#include <lib/rbtree.h>
#include <lib/string.h>
#include <vm/vmem.h>
#include <vm/page.h>
#include <vm/slab.h>
#include <vm/phys.h>
#include <vm/pressure.h>
#include <vm/mmu.h>
#include <vm/vm.h>
#include <config.h>

#define VMEM_NFREELIST (GB_SHIFT - PAGE_SHIFT - 1)

typedef struct vmem_free {
	size_t idx;
	list_node_t node;
	rb_node_t tree_node;
	vm_vaddr_t addr;
	vm_npages_t npages;
} vmem_free_t;

static list_t vmem_freelists[VMEM_NFREELIST];
static sync_t vmem_lock = SYNC_INIT(MUTEX);
static rb_tree_t vmem_tree = RB_TREE_INIT;
static DEFINE_VM_SLAB(vmem_slab, sizeof(vmem_free_t), 0);
static vmem_free_t vmem_init_free;

static vmem_free_t *vmem_get_free_at(vm_vaddr_t addr) {
	vmem_free_t *cur;

	/*
	 * The rb-tree is sorted by address.
	 */
	rb_search(&vmem_tree, cur, {
		if(addr >= cur->addr && addr <
			(cur->addr + ptoa(cur->npages)))
		{
			return cur;
		} else if(addr < cur->addr) {
			goto left;
		} else {
			goto right;
		}
	});

	return NULL;
}

static size_t vmem_freelist_idx(vm_npages_t npages) {
	size_t idx = sizeof(unsigned int) * 8 - __builtin_clz(npages);
	return min(idx - 1, VMEM_NFREELIST - 1);
}

static bool vmem_free_resize(vmem_free_t *free, vm_npages_t npages) {
	size_t idx;

	sync_assert(&vmem_lock);

	if(npages == 0) {
		list_remove(&vmem_freelists[free->idx], &free->node);
		rb_remove(&vmem_tree, &free->tree_node);

		/*
		 * The caller has to free the free region structure.
		 */
		return free != &vmem_init_free;
	} else {
		free->npages = npages;

		/*
		 * Now that the some pages where added to or removed from the
		 * free region, it may be on the wrong free-list
		 */
		idx = vmem_freelist_idx(free->npages);
		if(idx != free->idx) {
			list_remove(&vmem_freelists[free->idx], &free->node);

			/*
			 * TODO insert at back or at start depending on size
			 */
			list_add(&vmem_freelists[idx], &free->node);
			free->idx = idx;
		}

		return false;
	}
}

static void vmem_free_create(vmem_free_t *free, vm_vaddr_t addr,
	vm_npages_t npages)
{
	vmem_free_t *cur;

	free->idx = vmem_freelist_idx(npages);
	free->addr = addr;
	free->npages = npages;
	list_node_init(free, &free->node);
	rb_node_init(free, &free->tree_node);

	/*
	 * Insert into the rb-tree (sorted by address).
	 */
	rb_insert(&vmem_tree, cur, &free->tree_node, {
		if(free->addr < cur->addr) {
			goto left;
		} else {
			goto right;
		}
	});

	/*
	 * Insert into freelist.
	 */
	list_add(&vmem_freelists[free->idx], &free->node);
}

static void vmem_free_free(vmem_free_t *free) {
	list_node_destroy(&free->node);
	rb_node_destroy(&free->tree_node);
	vm_slab_free(&vmem_slab, free);
}

void vmem_debug(void) {
	vmem_free_t *free;

	kprintf("[vm] vmem: free regions:\n");
	rb_foreach_postorder(free, &free->tree_node, &vmem_tree) {
		kprintf("\t0x%x - 0x%x (index: %d)\n", free->addr, free->addr +
			ptoa(free->npages), free->idx);
	}
}

static vm_vaddr_t vmem_freelist_alloc(size_t idx, size_t npages, size_t size) {
	vmem_free_t *cur;
	vm_vaddr_t result;
	bool do_free;

	foreach(cur, &vmem_freelists[idx]) {
		if(cur->npages >= npages) {
			/*
			 * Decrement the free-virtual-memory-counter.
			 */
			vm_pressure_inc(VM_PR_MEM_KERN, size);

			/*
			 * The memory is allocate from the back of a free
			 * region.
			 */
			result = cur->addr + ptoa(cur->npages - npages);

			do_free = vmem_free_resize(cur, cur->npages - npages);
			sync_release(&vmem_lock);
			if(do_free) {
				vmem_free_free(cur);
			}

			asan_rmprot(result, size);
			return result;
		}
	}

	return VMEM_ERR_ADDR;
}

vm_vaddr_t vmem_alloc(vm_vsize_t size, vm_flags_t flags) {
	vm_npages_t npages = atop(size);
	size_t idx;

	VM_INIT_ASSERT(VM_INIT_VMEM);
	assert(ALIGNED(size, PAGE_SZ));
	assert(size > 0);

	sync_acquire(&vmem_lock);
	while(true) {
		/*
		 * Wait for free memory to become available.
		 */
		while(vm_mem_wait_p(VM_PR_MEM_KERN, size)) {
			sync_release(&vmem_lock);
			if(!VM_WAIT_P(flags)) {
				return VMEM_ERR_ADDR;
			}

			vm_mem_wait(VM_PR_MEM_KERN, size);
			sync_acquire(&vmem_lock);
		}

		/*
		 * Search through the freelists, until we find one region
		 * which has enough room for this allocation.
		 */
		for(idx = vmem_freelist_idx(npages); idx < VMEM_NFREELIST;
			idx++)
		{
			vm_vaddr_t ret = vmem_freelist_alloc(idx, npages, size);
			if(ret != VMEM_ERR_ADDR) {
				return ret;
			}
		}

		/*
	 	 * When allocating virtual memory greater than page-size, the
		 * allocation might fail, even if we waited for free memory
		 * above. In this case there would be enough virtual memory,
		 * but there is no contigous virtual memory region with the
		 * requested size available. Thus unlock the vmem_lock and wait
		 * until someone calls vmem_free().
		 */
		if(VM_WAIT_P(flags)) {
			vm_mem_wait_free(VM_PR_MEM_KERN, &vmem_lock);
		} else {
			sync_release(&vmem_lock);
			return VMEM_ERR_ADDR;
		}
	}
}

void vmem_free(vm_vaddr_t addr, vm_vsize_t size) {
	vm_npages_t npages = atop(size);
	vmem_free_t *free, *next;

	VM_INIT_ASSERT(VM_INIT_VMEM);
	assert(ALIGNED(addr, PAGE_SZ));
	assert(ALIGNED(size, PAGE_SZ));
	assert(addr > 0);
	assert(size > 0);

	asan_prot(addr, size);
	sync_acquire(&vmem_lock);

	/*
	 * The virtual memory sould not be backed by physical memory.
	 * Otherwise we might loose some physical pages.
	 */
#if CONFIGURED(INVARIANTS)
	for(size_t i = 0; i < size; i += PAGE_SZ) {
		assert(vmem_get_free_at(addr + i) == NULL);
		assert(!mmu_mapped(addr + i));
	}
#endif

	/*
	 * Increment the free-virtual-memory-counter.
	 */
	vm_pressure_dec(VM_PR_MEM_KERN, size);

	/*
	 * Collapse with previous free region if there is one.
	 */
	free = vmem_get_free_at(addr - 1);
	if(free) {
		vmem_free_resize(free, free->npages + npages);
	} else {
		/*
		 * The VM_SLAB_NOVALLOC prevents the slab allocator from
		 * calling vmem_alloc. Instead of calling vmem_alloc, its
		 * returns NULL when there is no slab with some free space left.
		 */
		while((free = vm_slab_alloc(&vmem_slab, VM_SLAB_NOVALLOC)) ==
			NULL)
		{
			/*
			 * Instead of using the virtual addresses being
			 * currently freed for a new vmem_free_t, prefer
			 * to use a page-sized virtual memory region to
			 * avoid unnecessary fragmentation.
			 */
			free = list_pop_front(&vmem_freelists[0]);
			if(free) {
				/*
				 * TODO VM_WAIT !!!!
				 * This problem is resolved once vm_slab_free
				 * starts caching its slabs (=> a VM_WAIT is ok
				 * in such cases or is it?)
				 *
				 * TODO once this VM_RESERVED thing is up and
				 * running we could allocate from reserved
				 * memory if normal memory fails.
				 */
				void *backed = vmem_back(free->addr, PAGE_SZ,
					VM_WAIT);

				assert(free->npages == 1);
				asan_rmprot(free->addr, PAGE_SZ);
				vm_slab_add_mem(&vmem_slab, backed, PAGE_SZ);
				break;
			} else {
				/*
				 * TODO VM_WAIT !!!!
				 */
				void *backed = vmem_back(addr, PAGE_SZ,
					VM_WAIT);

				/*s
				 * Provide the slab allocator with some memory,
				 * by using the virtual memory region currently
				 * being freed.
				 */
				asan_rmprot(addr, PAGE_SZ);
				vm_slab_add_mem(&vmem_slab, backed, PAGE_SZ);
				addr += PAGE_SZ;
				if(--npages == 0) {
					sync_release(&vmem_lock);
					return;
				}
			}
		}

		vmem_free_create(free, addr, npages);
	}

	/*
	 * Collapse with the next region if necessary.
	 */
	next = vmem_get_free_at(addr + ptoa(npages));
	if(next) {
		bool do_free;

		vmem_free_resize(free, free->npages + next->npages);
		do_free = vmem_free_resize(next, 0);
		sync_release(&vmem_lock);

		if(do_free) {
			vmem_free_free(next);
		}

		return;
	}

	sync_release(&vmem_lock);
}

void *vmem_back(vm_vaddr_t addr, vm_vsize_t size, vm_flags_t flags) {
	vm_flags_t map_flags = VM_PROT_RW | VM_PROT_KERN | (flags & VM_WAIT);
	void *ptr = (void *)addr;
	vm_paddr_t phys;
	int err;

	VM_FLAGS_CHECK(flags, VM_WAIT | VM_ZERO);
	for(vm_vsize_t i = 0; i < size; i += PAGE_SZ) {
		/*
		 * Rememver that vm_alloc_phys may be called safely
		 * during the vm bootstrap.
		 */
		phys = vm_alloc_phys(flags & VM_WAIT);
		if(phys == VM_PHYS_ERR) {
			vmem_unback(ptr, i);
			return VMEM_ERR_PTR;
		}

		err = mmu_map_kern(addr + i, PAGE_SZ, phys, map_flags,
			VM_MEMATTR_DEFAULT);
		if(err) {
			vm_free_phys(phys);
			vmem_unback(ptr, i);
			return VMEM_ERR_PTR;
		}
	}

	if(flags & VM_ZERO) {
		memset(ptr, 0x0, size);
	}

	return ptr;
}

void vmem_unback(void *ptr, vm_vsize_t size) {
	vm_vaddr_t addr = (vm_vaddr_t) ptr;
	vm_pgaddr_t phys;
	vm_page_t *page;

	/*
	 * Free the physical pages.
	 */
	for(vm_vsize_t i = 0; i < size; i += PAGE_SZ) {
		phys = vtophys(addr + i);
		mmu_unmap_kern(addr + i, PAGE_SZ);

		page = vm_phys_to_page(phys);
		vm_page_set_state(page, VM_PG_NORMAL);
		vm_page_free(page);
	}
}

void *vmem_alloc_backed(vm_vsize_t size, vm_flags_t flags) {
	vm_vaddr_t addr;
	void *ptr;

	VM_FLAGS_CHECK(flags, VM_WAIT | VM_ZERO);

	addr = vmem_alloc(size, flags & VM_WAIT);
	if(addr == VMEM_ERR_ADDR) {
		return VMEM_ERR_PTR;
	}

	/*
	 * Back the virtual memory with physical memory.
	 */
	ptr = vmem_back(addr, size, flags);
	if(ptr == VMEM_ERR_PTR) {
		vmem_free(addr, size);
	}

	return ptr;
}

void vmem_free_backed(void *ptr, vm_vsize_t size) {
	/*
	 * First free the physical pages and then free
	 * the virtual memory region.
	 */
	vmem_unback(ptr, size);
	vmem_free((vm_vaddr_t) ptr, size);
}

void vmem_init(vm_vaddr_t addr, vm_vaddr_t end) {
	vm_vsize_t size = end - addr;

	kprintf("[vm] vmem: initializing at 0x%x - 0x%x\n", addr, end);
	for(size_t i = 0; i < VMEM_NFREELIST; i++) {
		list_init(&vmem_freelists[i]);
	}

	vm_pr_mem_init(VM_PR_MEM_KERN, size, size);
	vmem_free_create(&vmem_init_free, addr, atop(size));
	vm_init_done(VM_INIT_VMEM);
}

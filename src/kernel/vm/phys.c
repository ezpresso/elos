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
#include <kern/sync.h>
#include <kern/init.h>
#include <kern/futex.h>
#include <sys/limits.h>
#include <vm/phys.h>
#include <vm/page.h>
#include <vm/vmem.h>
#include <vm/vm.h>
#include <vm/pressure.h>

/*
 * 		The physical memory manager
 * 		###########################
 *
 * Physical memory is allocated using an buddy allocator.
 *
 * 		Internal structures
 * 		###################
 *
 * Physical memory is split up into a number of vm_physeg_t s. These
 * segments contain an array of vm_page_t s. There is a vm_page_t for
 * each PAGE_SZ sized memory region in the segment. A vm_page_t may
 * actually describe more than just one PAGE_SZ region and
 * thus include memory from the folling page or pages.
 * The size of the range a page covers is calculated this way:
 *
 * 		size = (1 << (page->order + PAGE_SHIFT)
 *
 * This means that if a page has an order greater than zero, its physical
 * memory range includes the next page or pages. The pages covered by a
 * previous page have an order of PHYS_ORDER_NONE and may never be on any
 * free-list.
 */

/**
 * @brief The maximum number of physical segments.
 */
#define VM_PHYSEG_NUM 8

/**
 * The size can be big here because it's freed after boot anyway and
 * the structures are rather small.
 */
#define VM_PHYSRSV_NUM 64

/**
 * The page is currently not in the page hashtable if it's on a freelist
 * and thus the page hash node can be used here.
 * XXX this is ugly
 */
#define VM_PGNODE(page) (&(page)->node.node)

#define vm_phys_order_check(order) \
	kassert((order) < VM_PHYS_ORDER_NUM, "[vm] phys: invalid page " \
		"order: %d", (order));

typedef struct vm_phys_rsv {
	vm_paddr_t addr;
	vm_psize_t size;
} vm_phys_rsv_t;

typedef struct vm_physeg {
	vm_pgaddr_t start;
	vm_npages_t size;
	vm_page_t *pages;
} vm_physeg_t;

static list_t vm_freelist[VM_PHYS_ORDER_NUM];
static sync_t vm_phylock = SYNC_INIT(MUTEX);
static vm_npages_t vm_phys_total = 0;

static vm_physeg_t vm_physegs[VM_PHYSEG_NUM];
static size_t vm_nphyseg = 0;

static __initdata vm_phys_rsv_t vm_phys_rsv[VM_PHYSRSV_NUM];
static __initdata size_t vm_phys_nrsv = 0;

static vm_physeg_t *vm_physeg_get(vm_pgaddr_t addr) {
	vm_physeg_t *seg;

	for(size_t i = 0; i < vm_nphyseg; i++) {
		seg = &vm_physegs[i];

		if(addr >= seg->start && addr < seg->start + seg->size) {
			return seg;
		}
	}

	return NULL;
}

/**
 * @brief Return the number of pages a vm_page_t describes.
 */
static vm_npages_t vm_page_num(vm_page_t *page) {
	return 1U << page->order;
}

/**
 * @brief Return the number of bytes a vm_page_t describes.
 */
vm_psize_t vm_page_size(vm_page_t *page) {
	return 1U << (PAGE_SHIFT + page->order);
}

/**
 * Returns the first physical frame (physical address >> PAGE_SHIFT)
 * of the page's memory region!
 */
vm_pgaddr_t vm_page_addr(vm_page_t *page) {
	vm_physeg_t *seg;

	kassert(page->seg < VM_PHYSEG_NUM, "[vm] phys: invalid segment "
		"number: %d", page->seg);

	seg = &vm_physegs[page->seg];
	assert(seg->pages);
	return seg->start + (page - seg->pages);
}

static void vm_freelist_add(vm_page_t *page) {
	vm_phys_order_check(page->order);
	list_append(&vm_freelist[page->order], VM_PGNODE(page));
	vm_page_set_state(page, VM_PG_FREE);
}

/**
 * After calling vm_freelist_rem, page->order has to
 * be set to VM_PHYS_ORDER_NONE
 */
static void vm_freelist_rem(vm_page_t *page) {
	vm_phys_order_check(page->order);
	list_remove(&vm_freelist[page->order], VM_PGNODE(page));
	vm_page_set_state(page, VM_PG_NORMAL);
}

static vm_page_t *vm_freelist_pop(size_t order) {
	vm_pghash_node_t *pgh_node;

	vm_phys_order_check(order);

	/*
	 * Since we reuse the listnode of the page's pagehash-node,
	 * a pagehash-node is returned here rather than a page.
	 */
	pgh_node = list_pop_front(&vm_freelist[order]);
	if(pgh_node) {
		return PGH2PAGE(pgh_node);
	} else {
		return NULL;
	}
}

void vm_phys_reserve(vm_paddr_t addr, vm_psize_t size, const char *name) {
	vm_paddr_t end = addr + size;

	VM_NOT_INIT_ASSERT(VM_INIT_PHYS);
	if(name) {
		kprintf("[vm] phys: reserving \"%s\": 0x%x - 0x%x\n",
			name, addr, addr + size);
	}

	for(size_t i = 0; i < vm_phys_nrsv; i++) {
		vm_phys_rsv_t *rsv = &vm_phys_rsv[i];

		if(rsv->addr + rsv->size == addr) {
			rsv->size += size;
			return;
		} else if(rsv->addr == end) {
			rsv->addr = addr;
			rsv->size += size;
			return;
		} else if(addr >= rsv->addr && end <= rsv->addr + rsv->size) {
			return;
		}
	}

	if(vm_phys_nrsv == VM_PHYSRSV_NUM) {
		kpanic("[vm] phys: could not reserve 0x%x - 0x%x\n",
			addr, size);
	} else {
		vm_phys_rsv[vm_phys_nrsv].addr = addr;
		vm_phys_rsv[vm_phys_nrsv].size = size;
		vm_phys_nrsv++;
	}
}

static bool vm_page_reserved(vm_pgaddr_t addr) {
	vm_paddr_t phys = ptoa(addr);

	for(size_t i = 0; i < vm_phys_nrsv; i++) {
		vm_phys_rsv_t *rsv = &vm_phys_rsv[i];

		if(phys >= rsv->addr && phys < rsv->addr + rsv->size) {
			return true;
		}
	}

	return false;
}

void vm_physeg_add(vm_paddr_t addr, vm_psize_t size) {
	VM_NOT_INIT_ASSERT(VM_INIT_PHYS_EARLY);
	kassert(ALIGNED(addr, PAGE_SZ) && ALIGNED(size, PAGE_SZ),
		"[vm] phys: invalid segment range: 0x%x - 0x%x\n",
		addr, size);
	kassert(addr < VM_PHYS_MAX, NULL);
	kassert(VM_PHYS_MAX - addr >= size - 1, "[vm] phys: segment reaches "
		"beyond the maximum physical memory: addr: 0x%x size: 0x%x",
		addr, size);

	if(vm_nphyseg == VM_PHYSEG_NUM) {
		kprintf("[vm] phys: ignoring segment: 0x%x - 0x%x\n", addr,
			addr + size);
	} else {
		vm_physeg_t *seg;

		kprintf("[vm] phys: adding segment: 0x%x - 0x%x\n", addr,
			addr + size);

		/*
		 * Add the segment into the segent array.
		 */
		seg = &vm_physegs[vm_nphyseg++];
		seg->start = atop(addr);
		seg->size = atop(size);

		vm_phys_total += seg->size;
	}
}

/*
 * Not using recursion here because a thread's kernel stack is rather small.
 */
static vm_page_t *vm_page_alloc_order(uint8_t order) {
	const size_t size = 1U << (order + PAGE_SHIFT);
	vm_page_t *buddy, *page = NULL;

	sync_scope_acquire(&vm_phylock);
	if(vm_mem_wait_p(VM_PR_MEM_PHYS, size)) {
		return NULL;
	}

	for(int i = order; i < VM_PHYS_ORDER_NUM; i++) {
		page = vm_freelist_pop(i);
		if(page) {
			kassert(vm_page_is_free(page), NULL);
			vm_page_set_state(page, VM_PG_NORMAL);
			break;
		}
	}

	/*
	 * In this case there would be enough free memory, but no contigous
	 * memory region with the requested size was found.
	 *
	 * Calling vm_mem_wait would be useless, because it would not block
	 * (as I said there is enough free memory due to the vm_mem_wait_p
	 * above). Actually one would need to wait until sombody calls
	 * vm_page_free(), which can be achieved by calling vm_mem_wait_free.
	 *
	 * TODO implement that (vmem already implements this behaviour)
	 *
	 * This is low prio. Nobody in the kernel requests physical
	 * memory greater than PAGE_SZ, so this will currently not happen.
	 */
	if(!page) {
		kpanic("[vm] page: TODO");
		return NULL;
	}

	/*
	 * Split the page a view times and add the buddies
	 * to the appropriate freelist.
	 */
	while(page->order > order) {
		page->order--;

		/*
		 * Add the buddy to the free-list.
		 */
		buddy = page + vm_page_num(page);
		kassert(!vm_page_is_free(buddy), NULL);
		buddy->order = page->order;
		vm_freelist_add(buddy);
	}

	vm_pressure_inc(VM_PR_MEM_PHYS, size);
	return page;
}

vm_page_t *vm_page_alloc(vm_flags_t flags) {
	vm_page_t *page;

	VM_INIT_ASSERT(VM_INIT_PHYS);
	VM_FLAGS_CHECK(flags, VM_WAIT);
	while((page = vm_page_alloc_order(0)) == NULL && VM_WAIT_P(flags)) {
		vm_mem_wait(VM_PR_MEM_PHYS, PAGE_SZ);
	}

	return page;
}

void vm_page_free(vm_page_t *page) {
	vm_physeg_t *seg = &vm_physegs[page->seg];
	vm_pgaddr_t addr;
	vm_npages_t size;
	vm_page_t *buddy;
	uint8_t order;

	vm_page_assert_allocated(page);
	vm_page_assert_not_pinned(page);
	if(page->flags & ~VM_PG_STATE_MASK) {
		kpanic("[vm] phys: page flags were set while freeing: 0x%x",
			page->flags & ~VM_PG_STATE_MASK);
	}

	sync_acquire(&vm_phylock);
	vm_pressure_dec(VM_PR_MEM_PHYS, vm_page_size(page));

	/*
	 * Merge the page with the buddy pages if it's free.
	 */
	while(page->order < VM_PHYS_ORDER_MAX) {
		addr = vm_page_addr(page);
		size = vm_page_num(page);
		order = page->order;

		/*
		 * Get the index of the buddy page and check if it exists.
		 */
		addr ^= size;
		if(addr < seg->start || addr >= seg->start + seg->size) {
			break;
		}

		buddy = &seg->pages[addr - seg->start];
		if(buddy->order != page->order || !vm_page_is_free(buddy)) {
			break;
		}

		/*
		 * Remove the buddy from the free-list.
		 */
		vm_freelist_rem(buddy);

		/*
		 * Set an invalid order for both of the pages.
		 * Only the last of the two buddies will keep
		 * the invalid order (it is now a part of a
		 * bigger page)
		 */
		buddy->order = VM_PHYS_ORDER_NONE;
		page->order = VM_PHYS_ORDER_NONE;

		/*
		 * Get the index of the first of the two buddies.
		 */
		addr &= ~((size << 1) - 1);
		page = &seg->pages[addr - seg->start];

		/*
		 * Merged 2 pages -> double the size of the first page.
		 */
		page->order = order + 1;
	}

	vm_freelist_add(page);
	sync_release(&vm_phylock);
}

vm_page_t *vm_phys_to_page(vm_paddr_t addr) {
	vm_pgaddr_t pgaddr = atop(addr);
	vm_page_t *page;
	vm_physeg_t *seg;

	VM_INIT_ASSERT(VM_INIT_PHYS);
	seg = vm_physeg_get(pgaddr);
	kassert(seg, NULL);

	page = &seg->pages[pgaddr - seg->start];
	kassert(!vm_page_is_free(page), NULL);

	return page;
}

static __init bool vm_seg_overlap(vm_paddr_t a1, vm_psize_t s1,
	vm_paddr_t a2, vm_psize_t s2)
{
	/*
	 * If the second segment start after the first one or the first
	 * segment starts after the second segment they do not overlap;
	 */
	return !(a2 >= a1 + s1 || a1 >= a2 + s2);
}

/**
 * A very limited and dumb physical memory allocator used for bootstrapping
 * the real physical memory manager.
 */
static __init vm_paddr_t vm_phys_early_alloc(vm_psize_t size) {
	vm_paddr_t phys, end;
	vm_page_t *page;

	VM_INIT_ASSERT(VM_INIT_PHYS_EARLY);

	page = vm_page_alloc_order(0);
	if(page) {
		return vm_page_phys(page);
	}

	for(size_t i = 0; i < vm_nphyseg; i++) {
		/*
		 * Don't try using the early allocator on physical
		 * segments, which are already initialized. If the early
		 * allocator would be used here, it could sucessfully
		 * allocate memory, but the vm_pages (which are present
		 * after initialization) would still be marked as being
		 * free. This would cause all sorts of errors.
		 */
		if(vm_physegs[i].pages != NULL) {
			continue;
		}

		phys = ptoa(vm_physegs[i].start);
		end = phys + ptoa(vm_physegs[i].size);

		/*
		 * Watch out for reserved regions.
		 */
		while(phys + size <= end) {
			size_t j;

			for(j = 0; j < vm_phys_nrsv; j++) {
				vm_phys_rsv_t *rsv = &vm_phys_rsv[j];

				if(vm_seg_overlap(phys, size, rsv->addr,
					rsv->size))
				{
					/*
					 * Restart searching at the end of the
					 * reserved region.
					 */
					phys = rsv->addr + rsv->size;
					break;
				}
			}

			/*
			 * The region from _phys_ until _phys_ + _size_
			 * is inside a physical memory segment and it's not
			 * reserved. This means that it is free and can be used.
			 */
			if(j == vm_phys_nrsv) {
				goto found;
			}
		}
	}

	kpanic("[vm] phys: early allocation failed (0x%x)\n", size);

found:
	/*
	 * Reserve that memory region.
	 */
	vm_phys_reserve(phys, size, NULL);

	return phys;
}

vm_paddr_t vm_alloc_phys(vm_flags_t flags) {
	VM_FLAGS_CHECK(flags, VM_WAIT);

	if(VM_INIT_P(VM_INIT_PHYS)) {
		vm_page_t *page = vm_page_alloc(flags);
		if(page) {
			return vm_page_phys(page);
		} else {
			return VM_PHYS_ERR;
		}
	} else {
		/*
		 * Used during bootstrapping of the vm subsystem.
		 */
		return vm_phys_early_alloc(PAGE_SZ);
	}
}

void vm_free_phys(vm_paddr_t phys) {
	kassert(ALIGNED(phys, PAGE_SZ), "[vm] phys: freeing invalid "
		"address: 0x%x", phys);
	VM_INIT_ASSERT(VM_INIT_PHYS);

	vm_page_free(vm_phys_to_page(phys));
}

static void vm_page_debug(vm_page_t *page) {
	vm_paddr_t addr = vm_page_phys(page);
	vm_psize_t size = vm_page_size(page);

	kprintf("0x%08x - 0x%08x (%d)\n", addr, addr + size,
			page->order);
}

void vm_debug(void) {
	vm_page_t *page;
	bool print;

	for(int i = 0; i < VM_PHYS_ORDER_NUM; i++) {
		print = false;

		foreach(page, &vm_freelist[i]) {
			kassert(page->order == i, NULL);
			kassert(vm_page_is_free(page), NULL);

			if(!print) {
				kprintf("  order: %d\n", i + PAGE_SHIFT);
				print = true;
			}
			kprintf("\t");
			vm_page_debug(page);
		}
	}

	kprintf("[phys] end\n");
}

vm_psize_t vm_phys_get_total(void) {
	return ptoa(vm_phys_total);
}

vm_psize_t vm_phys_get_free(void) {
	return vm_mem_get_free(VM_PR_MEM_PHYS);
}

static __init void vm_physeg_init(vm_physeg_t *seg, size_t segnum) {
	for(vm_npages_t i = 0; i < seg->size; i++) {
		vm_page_init(&seg->pages[i], segnum);
	}

	/*
	 * Now that all of the pages are setup, they can be merged using the
	 * buddy algo.
	 */
	for(vm_npages_t i = 0; i < seg->size; i++) {
		if(!vm_page_reserved(seg->start + i)) {
			/*
			 * There would be smarter ways to do the merging when
			 * initializing the segment, but simply claiming the
			 * page was allocated and freeing the page is way
			 * simpler.
			 */
			vm_page_free(&seg->pages[i]);
		}
	}
}

void __init vm_phys_init(void) {
	size_t i;

	kprintf("[vm] phys: staring the buddy allocator\n");
	if(vm_nphyseg == 0) {
		kpanic("[vm] phys: did not detect any physical memory");
	}

	for(i = 0; i < VM_PHYS_ORDER_NUM; i++) {
		list_init(&vm_freelist[i]);
	}

	/*
	 * Allocate all of the memory needed before freeing all of the
	 * availablepages by vm_physeg_init().
	 */
	for(i = 0; i < vm_nphyseg; i++) {
		/*
		 * Allocate memory for the individual pages. vmem is
		 * actually capable of allocating memory in such an early
		 * point in the boot process (using vm_phys_early_alloc).
		 */
		vm_physegs[i].pages = vmem_alloc_backed(
			ALIGN(sizeof(vm_page_t) * vm_physegs[i].size, PAGE_SZ),
			VM_NOFLAG);
		if(vm_physegs[i].pages == VMEM_ERR_PTR) {
			kpanic("[vm] phys: no memory available for the "
				"page array (0x%x - 0x%x)",
				ptoa(vm_physegs[i].start),
				ptoa(vm_physegs[i].start + vm_physegs[i].size));
		}

		vm_physeg_init(&vm_physegs[i], i);
	}

	vm_init_done(VM_INIT_PHYS);
}

void __init vm_phys_init_early(void) {
	kprintf("[vm] phys: staring the bootstrap allocator\n");
	vm_init_done(VM_INIT_PHYS_EARLY);

	vm_pr_mem_init(VM_PR_MEM_PHYS, ptoa(vm_phys_total), 0);
}

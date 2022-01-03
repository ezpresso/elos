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
#include <kern/cpu.h>
#include <kern/fault.h>
#include <kern/mp.h>
#include <lib/string.h>
#include <vm/vm.h>
#include <vm/phys.h>
#include <vm/vas.h> /* needed for mmu_kern_ctx */
#include <vm/mmu.h>
#include <vm/vmem.h>
#include <vm/page.h>
#include <vm/kern.h>
#include <vm/layout.h>
#include <arch/x86.h>
#include <arch/frame.h>
#include <arch/interrupt.h>

#define mmu_is_current(ctx) ((ctx) == mmu_cur_ctx || (ctx) == mmu_kern_ctx)
#define mmu_assert_current(ctx)	assert(mmu_is_current(ctx));

/*
 * TODO This marco does not automatically increment the cur-value,
 * which is confusing. A better approach is needed.
 */
#define mmu_foreach(cur, pde, addr, size)		\
	for((cur) = (addr), (pde) = mmu_vtopde(addr);	\
		(cur) < (addr) + (size);		\
		(pde) = mmu_foreach_newpde(cur, addr) ?	\
			mmu_vtopde(cur) : (pde))

pde_t kern_pgdir[NPDE] __align(PAGE_SZ);

/*
 * The page tables of the kernel are preallocated. They just need about
 * 1MB, which is ok.
 * This array cannot be put into boot_vm.c, because this array would
 * not end up in bss then, it would be in the binary (adding 1MB full of zeroes
 * to the binary).
 */
pte_t kern_pagetables[NPDE_KERN][NPTE] __align(PAGE_SZ);

static inline bool mmu_foreach_newpde(vm_vaddr_t cur, vm_vaddr_t addr) {
	return cur == addr || ALIGNED(cur, LPAGE_SZ);
}

static vm_vaddr_t mmu_pde_end(vm_vaddr_t addr) {
	return (addr + LPAGE_SZ) & LPAGE_MASK;
}

static int mmu_pde_ref(mmu_ctx_t *ctx, vm_vaddr_t addr, vm_flags_t flags) {
	vm_page_t *page = NULL;
	pde_t *pde, tmp;
	vm_paddr_t phys;
	void *map;

	VM_FLAGS_CHECK(flags, VM_WAIT);

	/*
	 * The kernel PDEs are preallocated and are never freed.
	 */
	if(VM_IS_KERN(addr)) {
		return 0;
	}

	assert(ctx != mmu_kern_ctx);
	pde = mmu_vtopde(addr);

	sync_acquire(&ctx->lock);
	tmp = *pde;
	if((tmp & PG_P) == 0) {
		sync_release(&ctx->lock);

		/*
		 * Allocate a new page, which is used for the page table. The
		 * context is now unlocked, so it is possible that another
		 * thread concurrently tries to allocate a page for the same
		 * PDE.
		 */
		page = vm_page_alloc(flags);
		if(page == NULL) {
			return -ENOMEM;
		}

		/*
		 * The pin count is used as a reference counter for page tables.
		 */
		vm_page_pin(page);
		phys = vm_page_phys(page);

		/*
		 * Get the address where this PDE is mappedin the recursive
		 * mapping.
		 */
		map = mmu_vtopte(addr & LPAGE_MASK);

		/*
		 * Another thread might have allocted the page table in the
		 * meantime.
		 */
		sync_acquire(&ctx->lock);
		if(!(*pde & PG_P)) {
			*pde = phys | PG_P | PG_U | PG_W;

			invlpg((vm_vaddr_t)map);
			ipi_invlpg(mmu_kern_ctx, (vm_vaddr_t)map, PAGE_SZ);

			memset(map, 0x0, PAGE_SZ);
			sync_release(&ctx->lock);
			return 0;
		}
	}

	vm_page_pin(vm_phys_to_page(tmp & PAGE_MASK));
	sync_release(&ctx->lock);

	/*
	 * Free the page that was mistakenly allocated.
	 */
	if(page) {
		vm_page_unpin(page);
		vm_page_free(page);
	}

	return 0;
}

static bool mmu_pde_unref(mmu_ctx_t *ctx, uintptr_t addr, pde_t *pde,
	vm_page_t *page)
{
	sync_assert(&ctx->lock);
	if(VM_IS_KERN(addr)) {
		return false;
	}

	if(page == NULL) {
		page = vm_phys_to_page(*pde & PAGE_MASK);
	}

	if(vm_page_unpin(page) == 1) {
		vm_vaddr_t recur_addr;

		*pde = 0;

		/*
		 * Make sure that there are no errors with the recursive
		 * mapping.
		 */
		recur_addr = (vm_vaddr_t)mmu_vtopte(addr & LPAGE_MASK);
		invlpg(recur_addr);
		ipi_invlpg(mmu_kern_ctx, recur_addr, PAGE_SZ);

		/*
		 * Temporarily unlock the context to free the page.
		 */
		sync_release(&ctx->lock);

		/*
		 * TODO If the reference count of a PDE is zero, nothing is
		 * mapped in there and thus the page of the PDE is zeroed.
		 * => vm_page_free(page, VM_ZERO) as an optimization
		 */
		vm_page_free(page);
		sync_acquire(&ctx->lock);
		return true;
	} else {
		return false;
	}
}

static uint32_t mmu_map_flags(vm_flags_t flags, vm_memattr_t attr) {
	uint32_t res = PG_P;

	/*
	 * Write only not supported.
	 */
	kassert((flags & VM_PROT_RW) != VM_PROT_WR, "[mmu] writeonly mapping "
		"not supported: 0x%x", flags);

	/*
	 * Execute-only is not supported.
	 * TODO validate if upper layers call mmu_protect this way.
	 */
	kassert((flags & (VM_PROT_RD|VM_PROT_EXEC)) != VM_PROT_EXEC, "[mmu] "
		"execute only mapping not supported: 0x%x", flags);

	if(flags == VM_PROT_NONE) {
		return 0;
	}

	if(attr == VM_MEMATTR_UNCACHEABLE) {
		res |= PG_PCD;
	}

	res |= VM_PROT_WR_P(flags) ? PG_W : 0;
	res |= VM_PROT_USER_P(flags) ? PG_U : 0;

	return res;
}

int mmu_map_kern(vm_vaddr_t addr, vm_vsize_t size, vm_paddr_t paddr,
		vm_flags_t flags, vm_memattr_t attr)
{
	pde_t pte = mmu_map_flags(flags, attr);
	vm_vaddr_t end, cur, rest;

	cur = addr;
	rest = size;
	while(rest) {
		assert(VM_IS_KERN(cur));

		/*
		 * Map every page that is in this page table
		 */
		end = mmu_pde_end(cur);
		while(cur < end && rest) {
			*mmu_vtopte(cur) = pte | paddr;
			invlpg(cur);
			paddr += PAGE_SZ;
			cur += PAGE_SZ;
			rest -= PAGE_SZ;
		}
	}

	if((flags & MMU_MAP_CPULOCAL) == 0) {
		ipi_invlpg(mmu_kern_ctx, addr, size);
	}

	return 0;
}

int mmu_map_page(mmu_ctx_t *ctx, vm_vaddr_t addr, vm_page_t *page,
	vm_flags_t flags)
{
	vm_paddr_t phys = vm_page_phys(page);
	uint32_t mmu_flags;
	pte_t *pte;
	int err;

	VM_FLAGS_CHECK(flags, VM_WAIT | VM_PROT_RWX | VM_PROT_KERN |
		VM_PROT_USER);

	err = mmu_pde_ref(ctx, addr, flags & VM_WAIT);
	if(err) {
		return err;
	}

	mmu_flags = mmu_map_flags(flags, VM_MEMATTR_DEFAULT);
	assert(mmu_flags != 0);

	pte = mmu_vtopte(addr);
	*pte = phys | mmu_flags;

	invlpg(addr);
	ipi_invlpg(ctx, addr, PAGE_SZ);

	return 0;
}

void mmu_unmap(mmu_ctx_t *ctx, vm_vaddr_t addr, vm_vsize_t size) {
	vm_page_t *page = NULL;
	vm_vaddr_t cur;
	pte_t *pte;
	pde_t *pde;

	mmu_assert_current(ctx);
	assert(ctx != mmu_kern_ctx);

	sync_scope_acquire(&ctx->lock);
	mmu_foreach(cur, pde, addr, size) {
		if(mmu_foreach_newpde(cur, addr)) {
			/*
			 * Skip this 4MB region if nothing is mapped here.
			 */
			if(!(*pde & PG_P)) {
				cur = mmu_pde_end(cur);
				continue;
			} else {
				/*
				 * The page of the PDE is needed, because the
				 * reference count of the PDE is stored there.
				 */
				page = vm_phys_to_page(*pde & PAGE_MASK);
			}
		}

		pte = mmu_vtopte(cur);
		if(*pte & PG_P) {
			*pte = 0;
			invlpg(cur);

			/*
			 * If the PDE reaches a reference count of zero, there
			 * are no valid PTEs left in there, so don't continue
			 * searching for them in this PDE.
			 */
			if(mmu_pde_unref(ctx, cur, pde, page)) {
				cur = mmu_pde_end(cur);
				continue;
			}
		}

		cur += PAGE_SZ;
	}

	ipi_invlpg(ctx, addr, size);
}

void mmu_unmap_kern(vm_vaddr_t addr, vm_vsize_t size) {
	pte_t *pte;

	assert(VM_IS_KERN(addr));
	for(vm_vsize_t i = 0; i < size; i += PAGE_SZ) {
		pte = mmu_vtopte(addr + i);
		*pte = 0;
		invlpg(addr + i);
	}

	ipi_invlpg(mmu_kern_ctx, addr, size);
}

void mmu_unmap_page(mmu_ctx_t *ctx, vm_vaddr_t addr, vm_page_t *page) {
	vm_paddr_t phys = vm_page_phys(page);
	pte_t *pte, *map = NULL;
	bool inval = false;
	pde_t *pde;

	pde = &ctx->pgdir[addr >> LPAGE_SHIFT];

	sync_acquire(&ctx->lock);
	if(!(*pde & PG_P)) {
		sync_release(&ctx->lock);
		return;
	}

	if(mmu_is_current(ctx)) {
		pte = mmu_vtopte(addr);
	} else {
		/*
		 * When unmapping from a context not loaded, the pde
		 * needs to be temporarily mapped.
		 */
		map = vm_kern_map_quick(*pde & PAGE_MASK);
		pte = &map[(addr >> PAGE_SHIFT) & (NPDE - 1)];
	}

	if((*pte & PAGE_MASK) == phys) {
		*pte = 0;
		inval = true;
		mmu_pde_unref(ctx, addr, pde, NULL);
	}

	sync_release(&ctx->lock);
	if(inval) {
		invlpg(addr);
		ipi_invlpg(ctx, addr, PAGE_SZ);
	}

	/*
	 * Unmap the temporary mapping.
	 */
	if(!mmu_is_current(ctx)) {
		vm_kern_unmap_quick(map);
	}
}

void mmu_protect(mmu_ctx_t *ctx, vm_vaddr_t addr, vm_vsize_t size,
	vm_flags_t flags)
{
	uint32_t mmu_flags;
	vm_vaddr_t cur;
	pde_t *pde;
	pte_t *pte;

	VM_FLAGS_CHECK(flags, VM_PROT_MASK);
	mmu_assert_current(ctx);
	assert(ctx != mmu_kern_ctx);

	mmu_flags = mmu_map_flags(flags, VM_MEMATTR_DEFAULT);
	if(mmu_flags == 0) {
		mmu_unmap(ctx, addr, size);
		return;
	}

	mmu_foreach(cur, pde, addr, size) {
		if(mmu_foreach_newpde(cur, addr)) {
			sync_acquire(&ctx->lock);
			if(!(*pde & PG_P)) {
				sync_release(&ctx->lock);

				/*
				 * The pde is not present, which means
				 * nothing is currently mapped in this 4MB
				 * region. Thus we
				 * skip this region
				 */
				cur = mmu_pde_end(cur);
				continue;
			}

			sync_release(&ctx->lock);
		}

		pte = mmu_vtopte(cur);
		if(*pte & PG_P) {
			*pte = (*pte & ~(PG_P | PG_W | PG_U)) | mmu_flags;
		}

		/*
		 * TODO lazy inval
		 */
		invlpg(cur);
		cur += PAGE_SZ;
	}

	ipi_invlpg(ctx, addr, size);
}

bool mmu_mapped(vm_vaddr_t addr) {
	pde_t *pde = mmu_vtopde(addr);
	if(*pde & PG_P) {
		return !!(*mmu_vtopte(addr) & PG_P);
	} else {
		return false;
	}
}

static void mmu_fault(__unused int intr, trapframe_t *tf, __unused void *arg) {
	thread_t *thread = cur_thread();
	vm_flags_t flags = 0;
	uintptr_t addr;
	int err;

	assert(!cpu_intr_enabled());
	if(tf->err_code & PFE_RSVD) {
		kpanic("[mmu] error: reserved bits are set");
	}

	/*
	 * Get address that caused the fault.
	 */
	addr = cr2_get();

	/*
	 * Reenable interrupts once cr2 was read.
	 */
	sti();

#if 0
	kprintf("[vm] mmu: pgfault at 0x%x (code: 0x%x, at 0x%x)\n", addr,
		tf->err_code, tf->eip);
#endif

#if 0
	pte_t *pte = mmu_get_pte(addr);
	if(pte != NULL) {
		/* check for lazy inval */
	}
#endif

	if(tf->err_code & PFE_W) {
		flags |= VM_PROT_WR;
	} else {
		flags |= VM_PROT_RD;
	}

	if(tf->err_code & PFE_U) {
		flags |= VM_PROT_USER;
	} else {
		flags |= VM_PROT_KERN;
	}

	err = vm_fault(addr, flags);
	if(!err) {
		return;
	} else if(thread->onfault) {
		/*
		 * Handle copyin / copyout faults.
		 */
		tf_set_jmp_buf(tf, thread->onfault);
	} else if(!TF_USER(tf)) {
		kprintf("[mmu] kernel page fault: 0x%x (0x%x)\n", addr,
			tf->err_code);
		kprintf("\teip: 0x%x\n", tf->eip);
		kprintf("\teax: 0x%x ebx: 0x%x ecx: 0x%x\n", tf->eax, tf->ebx,
			tf->ecx);
		kprintf("\tedx: 0x%x edi: 0x%x esi: 0x%x\n", tf->edx, tf->edi,
			tf->esi);
		kprintf("\tesp: 0x%x ebp: 0x%x\n", tf->esp, tf->ebp);
		vm_vas_debug(vm_vas_current);
		kpanic("kernel page fault: 0x%x", tf->eip);
	} else {
		kprintf("SIGSEGV\n");
		thread->arch.cr2 = addr;
		vm_sigsegv();
	}
}

struct vm_page *mmu_vtopage(vm_vaddr_t addr) {
	vm_paddr_t phys;

	if(VM_IS_KERN(addr)) {
		phys = mmu_vtophys(addr);
	} else {
		mmu_ctx_t *ctx = mmu_cur_ctx;
		pde_t *pde = mmu_vtopde(addr);
		pte_t *pte = mmu_vtopte(addr);

		sync_scope_acquire(&ctx->lock);
		if((*pde & PG_P) == 0) {
			return NULL;
		} else {
			phys = *pte & PAGE_MASK;
		}
	}

	return vm_phys_to_page(phys);
}

void mmu_ctx_switch(mmu_ctx_t *ctx) {
	cr3_set(ctx->cr3);
}

void mmu_ctx_create(mmu_ctx_t *ctx) {
	assert(ctx != mmu_kern_ctx);

	sync_init(&ctx->lock, SYNC_MUTEX);
	ctx->pgdir = vmem_alloc_backed(PAGE_SZ, VM_WAIT | VM_ZERO);
	ctx->cr3 = vtophys(ctx->pgdir);

	/*
	 * Initialize the kernel parts of the pgdir
	 */
	for(size_t i = PDE_KERN; i < PDE_RECUR; i++) {
		vm_vaddr_t pde = (uintptr_t)kern_pagetables[i - PDE_KERN];
		ctx->pgdir[i] = PG_P | PG_W | (pde - KERNEL_VM_BASE);
	}

	/*
	 * Setup the recursive page directory entry.
	 */
	ctx->pgdir[PDE_RECUR] = ctx->cr3 | PG_P | PG_W;
}

void mmu_ctx_destroy(mmu_ctx_t *ctx) {
	vmem_free_backed(ctx->pgdir, PAGE_SZ);
	sync_destroy(&ctx->lock);
}

void mmu_map_ap(void) {
	/*
	 * Identity map the lowest 4MB.
	 */
	kern_pgdir[0] = PG_P | PG_W | PG_PS;
	invltlb();
}

void mmu_unmap_ap(void) {
	kern_pgdir[0] = 0;
	invltlb();
}

void mmu_init(void) {
	extern uintptr_t end;
	vm_paddr_t end_phys;

	/*
	 * The kernel page directory is statically allocated.
	 */
	mmu_kern_ctx->cr3 = (vm_vaddr_t)kern_pgdir - KERNEL_VM_BASE;
	mmu_kern_ctx->pgdir = kern_pgdir;

	/*
	 * Register page fault handler.
	 */
	cpu_set_intr_handler(INT_PF, mmu_fault, NULL);

	/*
	 * Unmap the identity map of lower memory used during kernel
	 * bootstrap.
	 */
	end_phys = (uintptr_t)&end - KERNEL_VM_BASE;
	for(vm_paddr_t i = 0; i < end_phys; i += LPAGE_SZ) {
		kern_pgdir[i >> LPAGE_SHIFT] = 0;
	}

	invltlb();
}

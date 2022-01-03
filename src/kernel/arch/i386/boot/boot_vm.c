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
#include <vm/mmu.h>
#include <vm/layout.h>
#include <arch/x86.h>

/*
 * Defined by mmu.c.
 */
extern pte_t kern_pagetables[NPDE_KERN][NPTE];
extern pde_t kern_pgdir[NPDE];

/*
 * Defined by the linker.
 */
extern uintptr_t end;

/*
 * Remember that this function is called in physical addressing mode
 * from boot.S. It is this function's task to enable virtual memory.
 */
asmlinkage __section(".boot_text") void init_boot_vm(void) {
	uintptr_t i, end_phys, cr3;
	pde_t *pgdir;

	/*
	 * These arrays reside in mmu.c and thus their virtual
	 * address is above KERNEL_VM_BASE, while they are loaded
	 * in the physical region just above 1MB with the rest of the
	 * kernel (see the linker script arch/i386/linker.ld).
	 * We have to convert those virtual addresses to the physical
	 * load addresses.
	 */
	pgdir = (void *)((uintptr_t)kern_pgdir - KERNEL_VM_BASE);
	end_phys = (uintptr_t)&end - KERNEL_VM_BASE;
	cr3 = (uintptr_t)pgdir;

	/*
	 * Enable 4MB pages. */
	cr4_set(cr4_get() | CR4_PSE);

	/*
	 * Identity map the kernel.
	 */
	for(i = 0; i < end_phys; i += LPAGE_SZ) {
		pgdir[i >> LPAGE_SHIFT] = PG_P | PG_W | PG_PS | i;
	}

	/*
	 * Setup the page tables of the kernel.
	 */
	for(i = 0; i < NPDE_KERN; i++) {
		pgdir[i + PDE_KERN] = PG_P | PG_W |
			((uintptr_t)kern_pagetables[i] - KERNEL_VM_BASE);
	}

	/*
	 * Recursive page directory!
	 */
	pgdir[PDE_RECUR] = PG_P | PG_W | cr3;

	/*
	 * Load the page directory and enable paging.
	 */
	cr3_set(cr3);
	cr0_set(cr0_get() | CR0_PG);

	/*
	 * Map the kernel (and the memory below the 1MB mark) in its real
	 * address space, but using the normal page size.
	 */
	for(i = KERNEL_VM_BASE; i < (uintptr_t)&end; i += PAGE_SZ) {
		*mmu_vtopte(i) = (i - KERNEL_VM_BASE) | PG_P | PG_W;
	}

	/*
	 * TODO Is this even needed?
	 */
	invltlb();
}

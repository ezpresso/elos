#ifndef VM_MMU_H
#define VM_MMU_H

#include <vm/flags.h>

/* Othern stuff that arch/mmu.h defines:
 * - struct mmu_ctx;
 * - mmu_ctx_t;
 * - vm_paddr_t mmu_vtophys(vm_vaddr_t addr);
 * - struct vm_page *mmu_vtopage(vm_vaddr_t addr);
 * - bool MMU_MAP_NO_WO
 */
#include <arch/mmu.h>

#define mmu_cur_ctx	(&vm_vas_current->mmu)
#define mmu_kern_ctx	(&vm_kern_vas.mmu)

/* These are convenient helpers around mmu_vtophys and mmu_vtopage,
 * which allow the caller to either specify a pointer or an integer
 * containing the address.
 */
#define vtophys(addr) mmu_vtophys((vm_vaddr_t)(addr))
#define vtopage(addr) mmu_vtopage((vm_vaddr_t)(addr))

/**
 * @brief Check if a virtual address is mapped.
 *
 * TODO BETTER: mmu_test(addr, flags)!
 */
bool mmu_mapped(vm_vaddr_t addr);

/**
 * @brief Map a virtual memory region to a physical memory region.
 *
 * @param addr 	The virtual address. Has to be page aligned.
 * @param size	The size of the mapping. Has to be page aligned.
 * @param paddr The start of physical region of the mapping. Has to be page
 *		aligned.
 * @param flags A combination of VM_PROT_RD, VM_PROT_WR (or VM_PROT_RW),
 *		VM_PROT_EXEC, VM_PROT_KERN or VM_PROT_USER, VM_WAIT
 * @param attr 	Memory attributes.
 *
 * @return	-ENOMEM	no memory available to fulfill the request 
 *			and the VM_WAIT flag was not set
 * 		0 	on success
 */
int mmu_map_kern(vm_vaddr_t addr, vm_vsize_t size, vm_paddr_t paddr,
	vm_flags_t flags, vm_memattr_t attr);

int mmu_map_page(mmu_ctx_t *ctx, vm_vaddr_t addr, struct vm_page *page,
	vm_flags_t flags);
void mmu_unmap_page(mmu_ctx_t *ctx, vm_vaddr_t addr, struct vm_page *page);

void mmu_unmap_kern(vm_vaddr_t addr, vm_vsize_t size);

void mmu_unmap(mmu_ctx_t *ctx, vm_vaddr_t addr, vm_vsize_t size);

void mmu_protect(mmu_ctx_t *ctx, vm_vaddr_t addr, vm_vsize_t size,
	vm_flags_t flags);

void mmu_ctx_switch(mmu_ctx_t *ctx);

void mmu_ctx_create(mmu_ctx_t *ctx);

void mmu_ctx_destroy(mmu_ctx_t *ctx);

#endif
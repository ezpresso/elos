#ifndef ARCH_MMU_H
#define ARCH_MMU_H

#include <arch/layout.h>
#include <kern/sync.h>

struct cpu;
struct vm_page;

#define MMU_MAP_CPULOCAL VM_FLAG1
#define MMU_MAP_NO_WO	true /* write only mappings not supported */

/* TODO 11.12 PAGE ATTRIBUTE TABLE (PAT) */

/* large pages */
#define LPAGE_SHIFT	22
#define LPAGE_SZ	(1 << LPAGE_SHIFT)
#define LPAGE_MASK	(~(LPAGE_SZ-1))

#define NPDE		1024U /* number of page directory entries per page dir */
#define NPTE		1024U /* number of page table entries per page table */

#define PDE_RECUR	(PT_ADDR >> PDE_SHIFT)

/**
 * Every page table entry (of the current context) is mapped in this region.
 */
#define PT_MAP		((pte_t *)PT_ADDR)

/* The page directory is mapped here */
#define PD_ADDR		(PT_ADDR + PDE_RECUR * PAGE_SZ)
#define PD_MAP		((pde_t *)PD_ADDR)

/* valid for pde and pte */
#define PG_P 		(1 << 0) /* present */
#define PG_W		(1 << 1) /* write */ 
#define PG_U 		(1 << 2) /* user-mode accesses */	
#define PG_PWT		(1 << 3) /* Page-level write-through */
#define PG_PCD		(1 << 4) /* Page-level cache disable */
#define PG_A		(1 << 5) /* Accessed */
/* valid for pte and pde (if used as 4MB page) */
#define PG_D 		(1 << 6) /* Dirty */
#define PG_G 		(1 << 8) /* Global */
#define PG_PDE_PAT	(1 << 12)
#define PG_PTE_PAT	(1 << 7)
/* valid for pde */
#define PG_PS 		(1 << 7) /* 1: 4MB page */

typedef uint32_t pde_t;
typedef uint32_t pte_t;

typedef struct mmu_ctx {
	sync_t lock;
	uintptr_t cr3;
	pde_t *pgdir;
} mmu_ctx_t;

static inline __always_inline pde_t *mmu_vtopde(vm_vaddr_t addr) {
	return PD_MAP + (addr >> LPAGE_SHIFT);
}

/* This has to be always inline because boot_vm.c calls this funcion. */
static inline __always_inline pte_t *mmu_vtopte(vm_vaddr_t addr) {
	return PT_MAP + (addr >> PAGE_SHIFT);
}

/**
 * Returns the physical address a virtual address from the current virtual
 * memory context maps to. It is assumed that the virtual address is mapped
 * and thus the page table exists. Does not work for 4MB pages.
 */
static inline vm_paddr_t mmu_vtophys(vm_vaddr_t addr) {
	return (*mmu_vtopte(addr) & PAGE_MASK) | (addr & ~PAGE_MASK);
}

void mmu_map_ap(void);
void mmu_unmap_ap(void);

struct vm_page *mmu_vtopage(vm_vaddr_t addr);

void mmu_init(void);

#endif
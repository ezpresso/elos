#ifndef VM_PHYS_H
#define VM_PHYS_H

#include <vm/flags.h>

/* -> max page-size = 0x400000 (if PAGE_SZ == 4kb) */
#define VM_PHYS_ORDER_NUM	11
#define VM_PHYS_ORDER_MAX	(VM_PHYS_ORDER_NUM-1)
#define VM_PHYS_ORDER_NONE	UINT8_MAX
#define VM_PHYS_ERR 		VM_PHYS_MAX

struct vm_page;

struct vm_page *vm_page_alloc(vm_flags_t flags);

vm_paddr_t vm_alloc_phys(vm_flags_t flags);
void vm_free_phys(vm_paddr_t phys);

void vm_page_free(struct vm_page *page);

vm_psize_t vm_page_size(struct vm_page *page);
vm_pgaddr_t vm_page_addr(struct vm_page *page);

static inline vm_paddr_t vm_page_phys(struct vm_page *page) {
	return ptoa(vm_page_addr(page));
}

struct vm_page *vm_phys_to_page(vm_paddr_t addr);

/**
 * @brief Get the number of total physical memory in the system.
 */
vm_psize_t vm_phys_get_total(void);

/**
 * @brief Get the number of free physical pages in the system.
 */
vm_psize_t vm_phys_get_free(void);

void vm_phys_reserve(vm_paddr_t addr, vm_psize_t size, const char *name);
void vm_physeg_add(vm_paddr_t addr, vm_psize_t size);

void vm_phys_init(void);

void vm_phys_init_early(void);

#endif
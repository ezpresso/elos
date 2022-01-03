#ifndef VM_SHPAGE_H
#define VM_SHPAGE_H

struct vm_vas;
struct vm_page;
struct vm_object;

/**
 * @brief Allocate space in the shared page.
 */
void *vm_shpage_alloc(size_t size, size_t align);

/**
 * @brief Get the page of the shared page.
 */
struct vm_page *vm_shpage_get(void);

/**
 * @brief Get the physical address of the shared page.
 */
vm_paddr_t vm_shpage_phys(void);

/**
 * @brief Get the userspace address of a shared page pointer.
 */
vm_vaddr_t vm_shpage_addr(void *ptr);

/**
 * @brief Map the shared page into a virtual address space.
 */
int vm_shpage_map(struct vm_vas *vas);

/**
 * @brief Initialize the shared-page subsystem.
 */
void vm_shpage_init(void);

#endif

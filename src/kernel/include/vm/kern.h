#ifndef VM_KERN_H
#define VM_KERN_H

#include <vm/flags.h>

struct vm_object;
struct vm_page;

/*
 * Defined by architecture code.
 */
void *vm_kern_map_quick(vm_paddr_t phys);
void vm_kern_unmap_quick(void *ptr);
void *vm_kern_map_phys_early(vm_paddr_t addr, vm_vsize_t size);
void vm_kern_unmap_phys_early(void *ptr, vm_vsize_t size);

void *vm_mapdev(vm_paddr_t phys, vm_vsize_t size, vm_memattr_t attr);
void vm_unmapdev(void *ptr, vm_vsize_t size);

/**
 * @brief Map a physical memory region somewhere in the kernel space.
 *
 * Defined by architecture code.
 */
int vm_kern_map_phys_attr(vm_paddr_t addr, vm_vsize_t size, vm_flags_t flags,
	vm_memattr_t attr, void **out);

/**
 * @brief Unmap a physical memory region mappped using vm_kern_map_phys(_attr).
*
 * Defined by architecture code.
 */
void vm_kern_unmap_phys(void *ptr, vm_vsize_t size);

/**
 * @brief Map a physical memory region somewhere in the kernel space.
 */
static inline int vm_kern_map_phys(vm_paddr_t addr, vm_vsize_t size,
	vm_flags_t flags, void **out)
{
	return vm_kern_map_phys_attr(addr, size, flags, VM_MEMATTR_DEFAULT, out);
}

/**
 * The default implementation of vm_kern_map_phys, i.e. allocate a virtual
 * memory region and ask the mmu-code to map it.
 */
int vm_kern_generic_map_phys(vm_paddr_t addr, vm_vsize_t size,
	vm_flags_t flags, vm_memattr_t attr, void **out);

/**
 * The generic implementation of vm_kern_unmap_phys, i.e. unmap and free the
 * virtual memory region.
 */
void vm_kern_generic_unmap_phys(void *ptr, vm_vsize_t size);

int vm_kern_map_object(struct vm_object *object, vm_vsize_t size,
	vm_objoff_t off, vm_flags_t flags, void **out);

void vm_kern_unmap_object(void *addr, vm_vsize_t size);

int vm_kern_map_page(struct vm_page *page, vm_flags_t flags, void **out);

void vm_kern_unmap_page(void *ptr);

/**
 * @brief Initialize the kernel virtual address space.
 */
void vm_kern_init(void);

#endif
#ifndef VM_VMEM_H
#define VM_VMEM_H

#include <vm/flags.h>

#define VMEM_ERR_ADDR 	(0)
#define VMEM_ERR_PTR	NULL

void vmem_debug(void);

/**
 * Allocate a contigous virtual memory region in the kernel space. The size of
 * this region is a multiple of PAGE_SZ and the resulting memory region is not
 * backed by physical memory and thus can not be accessed.
 *
 * @param flags VM_WAIT or 0
 */
vm_vaddr_t vmem_alloc(vm_vsize_t size, vm_flags_t flags);

/**
 * Free a virtual memory region. This region must have not necessarily
 * been allocated using vmem_alloc.
 */
void vmem_free(vm_vaddr_t addr, vm_vsize_t size);

/**
 * Allocate a virtual memory region in kernel space, which is backed by
 * physical memory.
 *
 * @param flags A combination of VM_WAIT and VM_ZERO
 */
void *vmem_alloc_backed(vm_vsize_t size, vm_flags_t flags);

/**
 * Free a memory region allocated using vmem_alloc_backed.
 */
void vmem_free_backed(void *ptr, vm_vsize_t size);

/**
 * Back a virtual memory region with physical memory.
 *
 * @param flags A combination of VM_WAIT and VM_ZERO
 */
void *vmem_back(vm_vaddr_t addr, vm_vsize_t size, vm_flags_t flags);

/**
 * Free the physical pages of a memory region.
 */
void vmem_unback(void *ptr, vm_vsize_t size);

/**
 * @brief Initialize the kernel's virtual memory allocator.
 */
void vmem_init(vm_vaddr_t addr, vm_vaddr_t end);

#endif
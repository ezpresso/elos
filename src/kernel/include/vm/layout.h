#ifndef VM_LAYOUT_H
#define VM_LAYOUT_H

#include <arch/layout.h>

#define KERNEL_VM_SIZE	(KERNEL_VM_END - KERNEL_VM_START + 1)
#define USER_VM_SIZE	(USER_VM_END - USER_VM_START + 1)

/**
 * @brief Check if a given memory address resides inside kernel space.
 */
#define VM_IS_KERN(addr) \
	((vm_vaddr_t)(addr) >= KERNEL_VM_START && \
	(vm_vaddr_t)(addr) <= KERNEL_VM_END)

/**
 * @brief Check if a given memory address resides inside user space.
 */
#define VM_IS_USER(addr) \
	((vm_vaddr_t)(addr) >= USER_VM_START && \
	(vm_vaddr_t)(addr) <= USER_VM_END)

/**
 * @brief Check if a given memory address resides inside user space.
 */
#define VM_REGION_IS_USER(addr, size) \
	(VM_IS_USER(addr) && (size == 0 || USER_VM_END - (addr) >= (size) - 1))

#endif

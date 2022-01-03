#ifndef ARCH_LAYOUT_H
#define ARCH_LAYOUT_H

#define PDE_SHIFT 		22
#define PDE_SIZE		(1 << 22)

/**
 * @brief The phyiscal load address of the kernel.
 */
#define KERNEL_LOAD_ADDR	0x00100000

/**
 * @brief The virtual base address of the kernel.
 */
#define KERNEL_VM_BASE		0xC0000000
#define KERNEL_VM_START		KERNEL_VM_BASE

/**
 * @brief The virtual end address size of the kernel.
 */
#define KERNEL_VM_END		0xFFFFFFFF

/**
 * @brief The start of the user virtual address space.
 */
#define USER_VM_START		0x00001000

/**
 * @brief The end of the user virtual address space.
 */
#define USER_VM_END		0xBFFFFFFF

/**
 * @brief The shared page resides at the end of userspace
 */
#define VM_SHAREDPAGE		(USER_VM_END - PAGE_SZ + 1)

/**
 * @brief The size of the stack of a new executable.
 */
#define VM_STACK_SIZE		(128 << KB_SHIFT)

/**
 * @brief The size of the stack of a new executable.
 */
#define VM_STACK_ADDR 		(VM_SHAREDPAGE - VM_STACK_SIZE)

/**
 * The recursive page mapping resides at the end of kernelspace. Every page
 * table entry (of the current context) is mapped in this region.
 */
#define PT_ADDR			(KERNEL_VM_END - PDE_SIZE + 1)

/**
 * @brief The number of page directory entries for the kernel.
 *
 * There is no PDE for the recursive mapping.
 */
#define NPDE_KERN		((KERNEL_VM_SIZE >> PDE_SHIFT) - 1)

/**
 * @brief The first pde in the kernel space.
 */
#define PDE_KERN 		(KERNEL_VM_START >> PDE_SHIFT)

/**
 * Vmem should not touch the recursive mapping
 */
#define VMEM_END		PT_ADDR

#define ASAN_START		(KERNEL_LOAD_ADDR + KERNEL_VM_BASE)
#define ASAN_END		VMEM_END

#endif

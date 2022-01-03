#ifndef ARCH_TYPES_H
#define ARCH_TYPES_H

typedef uintptr_t vm_vaddr_t;
typedef uintptr_t vm_vsize_t;

/**
 * @brief The maximum physical address.
 */
#define VM_PHYS_MAX UINT32_MAX

typedef uint32_t vm_paddr_t;
typedef uint32_t vm_psize_t;

typedef uint32_t vm_pgaddr_t; /* page address = atop(phys) = phys>>PAGE_SHIFT */
typedef uint32_t vm_npages_t;

#endif

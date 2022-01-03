#ifndef ARCH_VM_H
#define ARCH_VM_H

/**
 * On i386 pointers should be 4 byte aligned (not necessary for e.g. strings).
 */
#define VM_PTR_ALIGN		(1 << VM_PTR_ALING_LOG)
#define VM_PTR_ALING_LOG	2

vm_paddr_t vm_kern_phys_end(void);

void vm_detect_mem(void);
void vm_vmem_init(void);

#endif
#ifndef VM_VM_H
#define VM_VM_H

/* A quick note on the locking order in the vm subsys:
 * 1. vas->lock
 * 2. shadow->lock
 * 3. object->lock
 * 4. map->lock
 *
 * 1. vnode->lock
 * 2. vnode->object->lock
 */

#include <vm/flags.h>

/**
 * The arch header defines:
 * - VM_PHYS_MAX
 * - void vm_detect_mem()
 * - void vm_vmem_init()
 */
#include <arch/vm.h>

struct file;
struct vm_page;

#define VM_INIT_PHYS_EARLY	(1 << 0)
#define VM_INIT_MMU		(1 << 1)
#define VM_INIT_VMEM		(1 << 2)
#define VM_INIT_KMALLOC 	(1 << 3)
#define VM_INIT_PHYS		(1 << 4)

/* TODO */
#define VM_INIT_PGHASH
#define VM_INIT_PGOUT

#define VM_INIT_P(x) \
	((vm_init & (x)) == (x))

#define VM_INIT_ASSERT(x) \
	kassert(VM_INIT_P(x), "[vm] %d not initialized", (x))

#define VM_NOT_INIT_ASSERT(x) \
	kassert(!VM_INIT_P(x), "[vm] %d already initialized", (x))

typedef uint8_t vm_init_t;

extern vm_init_t vm_init;
extern void *vm_zero_map;
extern struct vm_page *vm_zero_page;

/**
 * @brief Handle a page fault.
 */
int vm_fault(vm_vaddr_t addr, vm_flags_t flags);
void vm_sigsegv(void);

/**
 * @brief Mark a vm subsystem as being initialized.
 */
static inline void vm_init_done(vm_init_t flag) {
	vm_init |= flag;
}

void vm_init_cpu(void);

/**
 * @brief Initialize the virtual memory manager.
 */
void init_vm(void);

#endif
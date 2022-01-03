#ifndef VM_PRESSURE_H
#define VM_PRESSURE_H

typedef enum vm_pressure {
	VM_PR_LOW = 0,
	VM_PR_MODERATE,
	VM_PR_HIGH,
} vm_pressure_t;

typedef enum vm_pr_mem_type {
	VM_PR_MEM_KERN = 0, /* Kernel virtual memory */
	VM_PR_MEM_PHYS, /* Physical memory */
	VM_PR_MEM_NUM,
} vm_pr_mem_type_t;

#define VM_PR_KERN	(1 << VM_PR_MEM_KERN)
#define VM_PR_PHYS	(1 << VM_PR_MEM_PHYS)
typedef uint8_t vm_pr_flags_t;

void vm_pressure_wait(vm_pr_flags_t flags, vm_pressure_t pr);

void vm_pressure_add(vm_pr_mem_type_t type, int64_t free);

static inline void vm_pressure_inc(vm_pr_mem_type_t type, uint64_t size) {
	assert(size < INT64_MAX);
	vm_pressure_add(type, -size);
}

static inline void vm_pressure_dec(vm_pr_mem_type_t type, uint64_t size) {
	assert(size < INT64_MAX);
	vm_pressure_add(type, size);
}

vm_pressure_t vm_pressure(vm_pr_flags_t flags);

uint64_t vm_mem_get_free(vm_pr_mem_type_t type);
bool vm_mem_wait_p(vm_pr_mem_type_t type, uint64_t size);
void vm_mem_wait(vm_pr_mem_type_t type, uint64_t size);

struct sync;
void vm_mem_wait_free(vm_pr_mem_type_t type, struct sync *lock);

void vm_pr_mem_init(vm_pr_mem_type_t type, uint64_t total, uint64_t free);

#endif
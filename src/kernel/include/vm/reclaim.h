#ifndef VM_RECLAIM_H
#define VM_RECLAIM_H

#include <kern/section.h>

#define VM_RECLAIM_SECTION section(vm_reclaim, vm_reclaim_t)
#define vm_reclaim(_name, callback) 				\
	static section_entry(VM_RECLAIM_SECTION) vm_reclaim_t	\
			vm_reclaim_ ## callback = { 		\
		.name = _name,					\
		.func = callback,				\
	}

typedef bool (vm_reclaim_func_t) (void);

typedef struct vm_reclaim {
	const char *name;
	list_node_t node;
	vm_reclaim_func_t *func;
} vm_reclaim_t;

void vm_reclaim_add(vm_reclaim_t *reclaim);
void vm_reclaim_rem(vm_reclaim_t *reclaim);
void vm_reclaim_launch(void);

#endif
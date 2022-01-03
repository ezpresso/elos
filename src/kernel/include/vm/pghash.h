#ifndef VM_PGHASH_H
#define VM_PGHASH_H

#include <vm/_pghash.h>

struct vm_object;
struct vm_page;

void vm_pghash_add(struct vm_object *obj, unsigned type, vm_objoff_t off,
		 vm_pghash_node_t *node);
void vm_pghash_rem(struct vm_object *obj, vm_pghash_node_t *node);
void vm_pghash_migrate(struct vm_object *old, vm_pghash_node_t *node,
		struct vm_object *new);

vm_pghash_node_t *vm_pghash_lookup(struct vm_object *obj, vm_objoff_t off);

void vm_pghash_init(void);

#endif
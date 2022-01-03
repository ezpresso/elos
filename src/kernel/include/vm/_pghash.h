#ifndef VM__PGHASH_H
#define VM__PGHASH_H

#include <lib/list.h>

#define VM_PGHASH_PAGE	0
#define VM_PGHASH_PAGER	1
#define VM_PGHASH_MASK	1

typedef struct vm_pghash_node {
	vm_objoff_t offset;
	list_node_t node;
	struct vm_object *object;
} vm_pghash_node_t;

static inline void vm_pghash_node_init(vm_pghash_node_t *node) {
	list_node_init(node, &node->node);
	node->offset = 0;
}

static inline void vm_pghash_node_destroy(vm_pghash_node_t *node) {
	list_node_destroy(&node->node);
}

static inline int vm_pghash_type(vm_pghash_node_t *node) {
	return node->offset & VM_PGHASH_MASK;
}

static inline vm_objoff_t vm_pghash_offset(vm_pghash_node_t *node) {
	return node->offset & ~VM_PGHASH_MASK;
}

static inline struct vm_object *vm_pghash_object(vm_pghash_node_t *node) {
	return node->object;
}

#endif

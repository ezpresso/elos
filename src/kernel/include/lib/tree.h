#ifndef LIB_TREE_H
#define LIB_TREE_H

#include <lib/list.h>

typedef struct tree_node {
	struct tree_node *parent;
	list_t children;
	list_node_t node;
} tree_node_t;

/**
 * @brief Loop through the children of a tree node
 */
#define tree_foreach(cur, parent) \
	foreach(cur, &(parent)->children)

/**
 * Initialize a tree node and associate a private
 * pointer with it. This pointer is returned when
 * calling tree_parent on a child of this node
 * for example. A tree node cannot be inserted
 * into the tree prior to initializing it.
 */
static inline void tree_node_init(void *ptr, tree_node_t *node) {
	list_node_init(ptr, &node->node);
	list_init(&node->children);
	node->parent = NULL;
}

static inline void tree_node_destroy(tree_node_t *node) {
	assert(node->parent == NULL);
	list_node_destroy(&node->node);
	list_destroy(&node->children);
}

/**
 * @brief Get the parent of a tree node.
 * @return 	NULL if @p node is the root of the tree
 *		else the pointer associated (using tree_node_init)
 *		with the parent is returned
 */
static inline void *tree_parent(tree_node_t *node) {
	if(node->parent) {
		return node->parent->node.value;
	} else {
		return NULL;
	}
}

/**
 * @brief Make a node a child of another node.
 */
static inline void tree_insert(tree_node_t *parent, tree_node_t *node) {
	assert(node->parent == NULL);
	list_append(&parent->children, &node->node);
	node->parent = parent;
}

/**
 * @brief Remove the node from the parent's children list.
 */
static inline void tree_remove(tree_node_t *parent, tree_node_t *node) {
	assert(node->parent == parent);
	list_remove(&parent->children, &node->node);
	node->parent = NULL;
}

/**
 * @brief Get the child of a node at @p idx
 */
static inline void *tree_get(tree_node_t *node, size_t idx) {
	return list_get(&node->children, idx);
}

/**
 * @brief Get the number of children of a node,
 */
static inline size_t tree_childnum(tree_node_t *node) {
	return list_length(&node->children);
}

#endif
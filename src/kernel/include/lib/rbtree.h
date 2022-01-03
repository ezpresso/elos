#ifndef LIB_RBTREE_H
#define LIB_RBTREE_H

#include <lib/development.h>


/*
 * This rbtree implementation is copied from linux.
 * Some small changes make the usage a little bit
 * different!
 * 
 * rbtree usage:
 * typedef struct entey {
 *	 rb_node_t node;
 *	 int i;
 * } entry_t;
 * 
 * void insert_entry(rb_tree_t *tree, entry_t *entry) {
 *	 entry_t *cur;
 *	 rb_insert(tree, cur, &entry->node, {
 *		 if(entry->i < cur->i) {
 *			 goto left;
 *		 } else {
 *			 goto right;
 *		 }		
 *	 });
 * }
 *
 * entry_t *find_entry(rb_tree_t *tree, int i) {
 *		entry_t *cur;
 * 		rb_search(tree, cur, {
 *			if(cur->i == i) {
 *				return cur;
 *			} else if(i < cur->i) {
 *				goto left;
 *			} else {
 *				goto right;
 *			}
 *		});
 *		return NULL
 * }
 *
 * void test(void) {
 *	 rb_tree_t tree = RB_TREE_INIT;
 *	 entry_t e1, e2;
 *
 *	 // Initialize data structures
 *	 rb_node_init(&e1, &e1.node);
 *	 rb_node_init(&e1, &e1.node);
 *
 *	 e1.i = 5;
 *	 e2.i = 7;
 *
 *	 insert_entry(&tree, &e1);
 *	 insert_entry(&tree, &e2);
 * }
 *
 */

#define RB_COLOR_MASK	0x1
#define	RB_RED			0x0
#define RB_BLACK		0x1
#define RB_TREE_INIT (rb_tree_t){ NULL }
#define __rb_parent(pc)		((rb_node_t *)(pc & ~0x3))
#define __rb_color(pc)		((pc) & RB_BLACK)
#define __rb_is_black(pc)	__rb_color(pc)
#define __rb_is_red(pc)		(!__rb_color(pc))
#define rb_color(rb)		__rb_color((rb)->pc)
#define rb_is_red(rb)		__rb_is_red((rb)->pc)
#define rb_is_black(rb)		__rb_is_black((rb)->pc)
#define rb_parent(rb)		__rb_parent((rb)->pc)
#define __rb_node_value(rb) ((rb) ? (rb)->value : NULL)

#define rb_node_assert_empty(node) {		\
	assert(rb_parent_node(node) == NULL);	\
	assert(rb_left_node(node) == NULL);	\
	assert(rb_right_node(node) == NULL);	\
}

#define rb_tree_destroy(node) ({	\
	assert((node)->root == NULL);	\
})

#define rb_node_destroy(node) ({	\
	rb_node_assert_empty(node);	\
	devel_rmprot(&(node)->value);	\
})

#define rb_foreach_postorder(cur, node, tree)	\
	for((cur) = rb_postorder_first(tree);	\
		(cur) != NULL;			\
		(cur) = rb_next_postorder(node))

#define rb_foreach(cur, node, tree)	\
	for((cur) = rb_first(tree);	\
		(cur) != NULL;		\
		(cur) = rb_next(node))

/* A weird macro, but it simplifies a lot. See top
 * of the header for example usage!!!
 */
#define rb_insert(tree, cur, node, code) {	\
	rb_node_t **lnk, *parent;		\
	rb_node_assert_empty(node);		\
	parent = NULL;				\
	lnk = &(tree)->root;			\
	while(*lnk) {				\
		parent = *lnk;			\
		cur = rb_node_value(parent);	\
		code				\
		notreached(); 			\
		right:				\
			lnk = &parent->right;	\
			continue;		\
		left:				\
			lnk = &parent->left;	\
	}					\
	__rb_link_node(node, parent, lnk);	\
	__rb_fixup_violation(tree, node);	\
}

#define rb_search(tree, cur, code) {		\
	rb_node_t *_node = (tree)->root;	\
	while(_node) {				\
		cur = rb_node_value(_node);	\
		code				\
		notreached(); 			\
		right:				\
			_node = _node->right;	\
			continue;		\
		left:				\
			_node = _node->left;	\
	}					\
}

typedef struct rb_node {
	uintptr_t pc;
	struct rb_node *right;
	struct rb_node *left;
	void *value;
} rb_node_t;

typedef struct rb_tree {
	rb_node_t *root;
} rb_tree_t;

void __rb_fixup_violation(rb_tree_t *tree, rb_node_t *node);
rb_node_t *rb_next_node(rb_node_t *node);
rb_node_t *rb_prev_node(rb_node_t *node);
rb_node_t *rb_first_node(const rb_tree_t *tree);
void rb_remove(rb_tree_t *tree, rb_node_t *node);

static inline void rb_tree_init(rb_tree_t *tree) {
	tree->root = NULL;
}

static inline void rb_node_init(void *val, rb_node_t *node) {
	node->value = val;
	node->right = NULL;
	node->left = NULL;
	node->pc = 0;

	devel_prot(&node->value);
}

static inline void *rb_node_value(rb_node_t *node) {
	return node->value;
}

static inline void *rb_root(rb_tree_t *tree) {
	return __rb_node_value(tree->root);
}

static inline void *rb_left(rb_node_t *node) {
	return __rb_node_value(node->left);
}

static inline void *rb_right(rb_node_t *node) {
	return __rb_node_value(node->right);
}

static inline rb_node_t *rb_parent_node(rb_node_t *node) {
	return rb_parent(node);
}

static inline rb_node_t *rb_left_node(rb_node_t *node) {
	return node->left;
}

static inline rb_node_t *rb_right_node(rb_node_t *node) {
	return node->right;
}

static inline void *rb_next(rb_node_t *node) {
	return __rb_node_value(rb_next_node(node));
}

static inline void *rb_prev(rb_node_t *node) {
	return __rb_node_value(rb_prev_node(node));
}

static inline void *rb_first(rb_tree_t *tree) {
	return __rb_node_value(rb_first_node(tree));
}

static inline void __rb_link_node(rb_node_t *node, rb_node_t *parent,
		rb_node_t **rb_link)
{
	node->pc = (uintptr_t)parent | RB_RED;
	node->left = NULL;
	node->right = NULL;
	*rb_link = node;
}

static inline rb_node_t *__rb_left_deepest_node(rb_node_t *node) {
	while(node->left || node->right) {
		if(node->left) {
			node = node->left;
		} else {
			node = node->right;
		}
	}

	return node;
}

static inline void *rb_get_left_deepest(rb_tree_t *tree) {
	if(!tree->root) {
		return NULL;
	} else {
		return __rb_left_deepest_node(tree->root)->value;
	}
}

static inline void *rb_postorder_first(rb_tree_t *tree) {
	return rb_get_left_deepest(tree);
}

static inline void *rb_next_postorder(rb_node_t *node) {
	rb_node_t *parent;

	if(!node || (parent = rb_parent(node)) == NULL) {
		return NULL;
	} else if(node == parent->left && parent->right) {
		return __rb_left_deepest_node(parent->right)->value;
	} else {
		return parent->value;
	}
}

#endif
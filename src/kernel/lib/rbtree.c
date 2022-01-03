/* This code is a modified version of linux's rbtree.
 *
 * Red Black Trees
 * (C) 1999  Andrea Arcangeli <andrea@suse.de>
 * (C) 2002  David Woodhouse <dwmw2@infradead.org>
 * (C) 2012  Michel Lespinasse <walken@google.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * linux/lib/rbtree.c
 */

#include <kern/system.h>
#include <lib/rbtree.h>

#define WRITE_ONCE(x, y) (x) = (y)

/*
 * red-black trees properties:  http://en.wikipedia.org/wiki/Rbtree
 *
 *  1) A node is either red or black
 *  2) The root is black
 *  3) All leaves (NULL) are black
 *  4) Both children of every red node are black
 *  5) Every simple path from root to leaves contains the same number
 *     of black nodes.
 *
 *  4 and 5 give the O(log n) guarantee, since 4 implies you cannot have two
 *  consecutive red nodes in a path and every red node is therefore followed by
 *  a black. So if B is the number of black nodes on every simple path (as per
 *  5), then the longest possible path due to 4 is 2B.
 *
 *  We shall indicate color with case, where black nodes are uppercase and red
 *  nodes will be lowercase. Unknown color nodes shall be drawn as red within
 *  parentheses and have some accompanying text comment.
 */

/*
 * Notes on lockless lookups:
 *
 * All stores to the tree structure (rb_left and rb_right) must be done using
 * WRITE_ONCE(). And we must not inadvertently cause (temporary) loops in the
 * tree structure as seen in program order.
 *
 * These two requirements will allow lockless iteration of the tree -- not
 * correct iteration mind you, tree rotations are not atomic so a lookup might
 * miss entire subtrees.
 *
 * But they do guarantee that any such traversal will only see valid elements
 * and that it will indeed complete -- does not get stuck in a loop.
 *
 * It also guarantees that if the lookup returns an element it is the 'correct'
 * one. But not returning an element does _NOT_ mean it's not present.
 *
 * NOTE:
 *
 * Stores to __rb_parent_color are not important for simple lookups so those
 * are left undone as of now. Nor did I check for loops involving parent
 * pointers.
 */

static inline void rb_set_parent(rb_node_t *rb, rb_node_t *p) {
	rb->pc = rb_color(rb) | (uintptr_t)p;
}

static inline void rb_set_parent_color(rb_node_t *rb, rb_node_t *p, int color) {
	rb->pc = (uintptr_t)p | color;
}

static inline void rb_set_black(rb_node_t *rb) {
	rb->pc |= RB_BLACK;
}

static inline rb_node_t *rb_red_parent(rb_node_t *red) {
	return (rb_node_t *)red->pc;
}

/*
 * This function returns the first node (in sort order) of the tree.
 */
rb_node_t *rb_first_node(const rb_tree_t *tree) {
	rb_node_t *n;

	n = tree->root;
	if(n) {
		while(n->left) {
			n = n->left;
		}
	}

	return n;
}

rb_node_t *rb_next_node(rb_node_t *node) {
	rb_node_t *parent;

	/*
	 * If we have a right-hand child, go down and then left as far
	 * as we can.
	 */
	if(node->right) {
		node = node->right;

		while(node->left) {
			node = node->left;
		}

		return node;
	}

	/*
	 * No right-hand children. Everything down and left is smaller than us,
	 * so any 'next' node must be in the general direction of our parent.
	 * Go up the tree; any time the ancestor is a right-hand child of its
	 * parent, keep going up. First time it's a left-hand child of its
	 * parent, said parent is our 'next' node.
	 */
	while((parent = rb_parent(node)) != NULL && node == parent->right) {
		node = parent;
	}
	
	return parent;
}

rb_node_t *rb_prev_node(rb_node_t *node) {
	rb_node_t *parent;

	/*
	 * If we have a left-hand child, go down and then right as far
	 * as we can.
	 */
	if(node->left) {
		node = node->left;
		while(node->right) {
			node = node->right;
		}

		return node;
	}

	/*
	 * No left-hand children. Go up till we find an ancestor which
	 * is a right-hand child of its parent.
	 */
	while((parent = rb_parent(node)) != NULL && node == parent->left) {
		node = parent;
	}

	return parent;
}

static inline void rb_change_child(rb_node_t *old, rb_node_t *new,
	rb_node_t *parent, rb_tree_t *tree)
{
	if(parent) {
		if(parent->left == old) {
			WRITE_ONCE(parent->left, new);
		} else {
			WRITE_ONCE(parent->right, new);
		}
	} else {
		WRITE_ONCE(tree->root, new);
	}
}

/*
 * Helper function for rotations:
 * - old's parent and color get assigned to new
 * - old gets assigned new as a parent and 'color' as a color.
 */
static inline void rb_rotate_set_parents(rb_node_t *old, rb_node_t *new,
	rb_tree_t *tree, int color)
{
	rb_node_t *parent = rb_parent(old);
	new->pc = old->pc;
	rb_set_parent_color(old, new, color);
	rb_change_child(old, new, parent, tree);
}

void __rb_fixup_violation(rb_tree_t *tree, rb_node_t *node) {
	rb_node_t *parent = rb_red_parent(node), *gparent, *tmp;

	while(true) {
		/*
		 * Loop invariant: node is red.
		 */
		if(unlikely(!parent)) {
			/*
			 * The inserted node is root. Either this is the
			 * first node, or we recursed at Case 1 below and
			 * are no longer violating 4).
			 */
			rb_set_parent_color(node, NULL, RB_BLACK);
			break;
		}

		/*
		 * If there is a black parent, we are done.
		 * Otherwise, take some corrective action as,
		 * per 4), we don't want a red root or two
		 * consecutive red nodes.
		 */
		if(rb_is_black(parent)) {
			break;
		}

		gparent = rb_red_parent(parent);

		tmp = gparent->right;
		if(parent != tmp) {	/* parent == gparent->left */
			if(tmp && rb_is_red(tmp)) {
				/*
				 * Case 1 - node's uncle is red (color flips).
				 *
				 *       G            g
				 *      / \          / \
				 *     p   u  -->   P   U
				 *    /            /
				 *   n            n
				 *
				 * However, since g's parent might be red, and
				 * 4) does not allow this, we need to recurse
				 * at g.
				 */
				rb_set_parent_color(tmp, gparent, RB_BLACK);
				rb_set_parent_color(parent, gparent, RB_BLACK);
				node = gparent;
				parent = rb_parent(node);
				rb_set_parent_color(node, parent, RB_RED);
				continue;
			}

			tmp = parent->right;
			if(node == tmp) {
				/*
				 * Case 2 - node's uncle is black and node is
				 * the parent's right child (left rotate at
				 * parent).
				 *
				 *      G             G
				 *     / \           / \
				 *    p   U  -->    n   U
				 *     \           /
				 *      n         p
				 *
				 * This still leaves us in violation of 4), the
				 * continuation into Case 3 will fix that.
				 */
				tmp = node->left;
				WRITE_ONCE(parent->right, tmp);
				WRITE_ONCE(node->left, parent);
				if(tmp) {
					rb_set_parent_color(tmp, parent,
						RB_BLACK);
				}
				rb_set_parent_color(parent, node, RB_RED);
				parent = node;
				tmp = node->right;
			}

			/*
			 * Case 3 - node's uncle is black and node is
			 * the parent's left child (right rotate at gparent).
			 *
			 *        G           P
			 *       / \         / \
			 *      p   U  -->  n   g
			 *     /                 \
			 *    n                   U
			 */
			WRITE_ONCE(gparent->left, tmp); /* == parent->right */
			WRITE_ONCE(parent->right, gparent);
			if(tmp) {
				rb_set_parent_color(tmp, gparent, RB_BLACK);
			}
			rb_rotate_set_parents(gparent, parent, tree, RB_RED);
			break;
		} else {
			tmp = gparent->left;
			if(tmp && rb_is_red(tmp)) {
				/* Case 1 - color flips */
				rb_set_parent_color(tmp, gparent, RB_BLACK);
				rb_set_parent_color(parent, gparent, RB_BLACK);
				node = gparent;
				parent = rb_parent(node);
				rb_set_parent_color(node, parent, RB_RED);
				continue;
			}

			tmp = parent->left;
			if(node == tmp) {
				/* Case 2 - right rotate at parent */
				tmp = node->right;
				WRITE_ONCE(parent->left, tmp);
				WRITE_ONCE(node->right, parent);
				if(tmp) {
					rb_set_parent_color(tmp, parent,
						RB_BLACK);
				}
				rb_set_parent_color(parent, node, RB_RED);
				parent = node;
				tmp = node->left;
			}

			/* Case 3 - left rotate at gparent */
			WRITE_ONCE(gparent->right, tmp); /* == parent->left */
			WRITE_ONCE(parent->left, gparent);
			if(tmp) {
				rb_set_parent_color(tmp, gparent, RB_BLACK);
			}
			rb_rotate_set_parents(gparent, parent, tree, RB_RED);
			break;
		}
	}
}

/*
 * Inline version for rb_erase() use - we want to be able to inline
 * and eliminate the dummy_rotate callback there
 */
static void rb_erase_color(rb_node_t *parent, rb_tree_t *tree) {
	rb_node_t *node = NULL, *sibling, *tmp1, *tmp2;

	while(true) {
		/*
		 * Loop invariants:
		 * - node is black (or NULL on first iteration)
		 * - node is not the root (parent is not NULL)
		 * - All leaf paths going through parent and node have a
		 *   black node count that is 1 lower than other leaf paths.
		 */
		sibling = parent->right;
		if(node != sibling) {	/* node == parent->left */
			if(rb_is_red(sibling)) {
				/*
				 * Case 1 - left rotate at parent
				 *
				 *     P               S
				 *    / \             / \
				 *   N   s    -->    p   Sr
				 *      / \         / \
				 *     Sl  Sr      N   Sl
				 */
				tmp1 = sibling->left;
				WRITE_ONCE(parent->right, tmp1);
				WRITE_ONCE(sibling->left, parent);
				rb_set_parent_color(tmp1, parent, RB_BLACK);
				rb_rotate_set_parents(parent, sibling, tree,
					RB_RED);
				sibling = tmp1;
			}
			tmp1 = sibling->right;
			if(!tmp1 || rb_is_black(tmp1)) {
				tmp2 = sibling->left;
				if(!tmp2 || rb_is_black(tmp2)) {
					/*
					 * Case 2 - sibling color flip
					 * (p could be either color here)
					 *
					 *    (p)           (p)
					 *    / \           / \
					 *   N   S    -->  N   s
					 *      / \           / \
					 *     Sl  Sr        Sl  Sr
					 *
					 * This leaves us violating 5) which
					 * can be fixed by flipping p to black
					 * if it was red, or by recursing at p.
					 * p is red when coming from Case 1.
					 */
					rb_set_parent_color(sibling, parent,
						RB_RED);
					if(rb_is_red(parent)) {
						rb_set_black(parent);
					} else {
						node = parent;
						parent = rb_parent(node);
						if(parent)
							continue;
					}
					break;
				}
				/*
				 * Case 3 - right rotate at sibling
				 * (p could be either color here)
				 *
				 *   (p)           (p)
				 *   / \           / \
				 *  N   S    -->  N   sl
				 *     / \             \
				 *    sl  Sr            S
				 *                       \
				 *                        Sr
				 *
				 * Note: p might be red, and then both
				 * p and sl are red after rotation(which
				 * breaks property 4). This is fixed in
				 * Case 4 (in rb_rotate_set_parents()
				 *         which set sl the color of p
				 *         and set p RB_BLACK)
				 *
				 *   (p)            (sl)
				 *   / \            /  \
				 *  N   sl   -->   P    S
				 *       \        /      \
				 *        S      N        Sr
				 *         \
				 *          Sr
				 */
				tmp1 = tmp2->right;
				WRITE_ONCE(sibling->left, tmp1);
				WRITE_ONCE(tmp2->right, sibling);
				WRITE_ONCE(parent->right, tmp2);
				if(tmp1) {
					rb_set_parent_color(tmp1, sibling,
						RB_BLACK);
				}
				tmp1 = sibling;
				sibling = tmp2;
			}
			/*
			 * Case 4 - left rotate at parent + color flips
			 * (p and sl could be either color here.
			 *  After rotation, p becomes black, s acquires
			 *  p's color, and sl keeps its color)
			 *
			 *      (p)             (s)
			 *      / \             / \
			 *     N   S     -->   P   Sr
			 *        / \         / \
			 *      (sl) sr      N  (sl)
			 */
			tmp2 = sibling->left;
			WRITE_ONCE(parent->right, tmp2);
			WRITE_ONCE(sibling->left, parent);
			rb_set_parent_color(tmp1, sibling, RB_BLACK);
			if(tmp2) {
				rb_set_parent(tmp2, parent);
			}
			rb_rotate_set_parents(parent, sibling, tree, RB_BLACK);
			break;
		} else {
			sibling = parent->left;

			if(rb_is_red(sibling)) {
				/* Case 1 - right rotate at parent */
				tmp1 = sibling->right;
				WRITE_ONCE(parent->left, tmp1);
				WRITE_ONCE(sibling->right, parent);
				rb_set_parent_color(tmp1, parent, RB_BLACK);
				rb_rotate_set_parents(parent, sibling, tree,
					RB_RED);
				sibling = tmp1;
			}

			tmp1 = sibling->left;
			if(!tmp1 || rb_is_black(tmp1)) {
				tmp2 = sibling->right;
				if(!tmp2 || rb_is_black(tmp2)) {
					/* Case 2 - sibling color flip */
					rb_set_parent_color(sibling, parent,
						RB_RED);
					if(rb_is_red(parent)) {
						rb_set_black(parent);
					} else {
						node = parent;
						parent = rb_parent(node);
						if(parent)
							continue;
					}
					break;
				}
				/* Case 3 - left rotate at sibling */
				tmp1 = tmp2->left;
				WRITE_ONCE(sibling->right, tmp1);
				WRITE_ONCE(tmp2->left, sibling);
				WRITE_ONCE(parent->left, tmp2);
				if(tmp1) {
					rb_set_parent_color(tmp1, sibling,
						RB_BLACK);
				}

				tmp1 = sibling;
				sibling = tmp2;
			}
			/* Case 4 - right rotate at parent + color flips */
			tmp2 = sibling->right;
			WRITE_ONCE(parent->left, tmp2);
			WRITE_ONCE(sibling->right, parent);
			rb_set_parent_color(tmp1, sibling, RB_BLACK);

			if(tmp2) {
				rb_set_parent(tmp2, parent);
			}

			rb_rotate_set_parents(parent, sibling, tree, RB_BLACK);
			break;
		}
	}
}

static rb_node_t *rb_erase(rb_node_t *node, rb_tree_t *tree) {
	rb_node_t *child = node->right;
	rb_node_t *tmp = node->left;
	rb_node_t *parent, *rebalance;
	uintptr_t pc;

#if 0
	if(leftmost && node == *leftmost)
		*leftmost = rb_next(node);
#endif

	if(!tmp) {
		/*
		 * Case 1: node to erase has no more than 1 child (easy!)
		 *
		 * Note that if there is one child it must be red due to 5)
		 * and node must be black due to 4). We adjust colors locally
		 * so as to bypass rb_erase_color() later on.
		 */
		pc = node->pc;
		parent = __rb_parent(pc);
		rb_change_child(node, child, parent, tree);
		if(child) {
			child->pc = pc;
			rebalance = NULL;
		} else {
			rebalance = __rb_is_black(pc) ? parent : NULL;
		}

		tmp = parent;
	} else if(!child) {
		/* Still case 1, but this time the child is node->left */
		tmp->pc = pc = node->pc;
		parent = __rb_parent(pc);
		rb_change_child(node, tmp, parent, tree);
		rebalance = NULL;
		tmp = parent;
	} else {
		rb_node_t *successor = child, *child2;

		tmp = child->left;
		if(!tmp) {
			/*
			 * Case 2: node's successor is its right child
			 *
			 *    (n)          (s)
			 *    / \          / \
			 *  (x) (s)  ->  (x) (c)
			 *        \
			 *        (c)
			 */
			parent = successor;
			child2 = successor->right;
		} else {
			/*
			 * Case 3: node's successor is leftmost under
			 * node's right child subtree
			 *
			 *    (n)          (s)
			 *    / \          / \
			 *  (x) (y)  ->  (x) (y)
			 *      /            /
			 *    (p)          (p)
			 *    /            /
			 *  (s)          (c)
			 *    \
			 *    (c)
			 */
			do {
				parent = successor;
				successor = tmp;
				tmp = tmp->left;
			} while(tmp);
			child2 = successor->right;
			WRITE_ONCE(parent->left, child2);
			WRITE_ONCE(successor->right, child);
			rb_set_parent(child, successor);
		}

		tmp = node->left;
		WRITE_ONCE(successor->left, tmp);
		rb_set_parent(tmp, successor);

		pc = node->pc;
		tmp = __rb_parent(pc);
		rb_change_child(node, successor, tmp, tree);

		if(child2) {
			successor->pc = pc;
			rb_set_parent_color(child2, parent, RB_BLACK);
			rebalance = NULL;
		} else {
			uintptr_t pc2 = successor->pc;
			successor->pc = pc;
			rebalance = __rb_is_black(pc2) ? parent : NULL;
		}

		tmp = successor;
	}

	return rebalance;
}

void rb_remove(rb_tree_t *tree, rb_node_t *node) {
	rb_node_t *rebalance = rb_erase(node, tree);
	if(rebalance) {
		rb_erase_color(rebalance, tree);
	}

	node->left = NULL;
	node->right = NULL;
	node->pc = 0;
}

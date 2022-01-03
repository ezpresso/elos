#ifndef LIB_MMAN_H
#define LIB_MMAN_H

#include <lib/rbtree.h>

/**
 * @brief Iterate through the nodes of a memory manager.
 *
 * Iterate through the nodes of a memory manager in ascending order.
 * The first node is the dummy node used for keeping track of the first
 * free hole and thus this node is skipped in the iteration.
 */
#define mman_foreach(n, mman)					\
	for((n) = rb_next(rb_first_node(&(mman)->nodes));	\
		(n) != NULL;					\
		(n) = mman_node_next(n))

typedef struct mman_node {
	rb_node_t freenode;
	rb_node_t node;
	uint64_t addr;
	uint64_t size;
	uint64_t free;
} mman_node_t;

typedef struct mman {
	rb_tree_t nodes;
	rb_tree_t free;
	mman_node_t node;
} mman_t;

/**
 * @brief Initialize a memory manager.
 *
 * A memory manager can be used for allocating addresses inside an
 * contigous address region.
 *
 * @param mman The mman structure to be initiaized.
 * @param addr The start address of the managed address region.
 * @param size The size of the managed address region
 */
void mman_init(mman_t *mman, uint64_t addr, uint64_t size);

/**
 * @brief Destroy a memory manager.
 *
 * Destroy a memory manager. There must not be any allocation inside
 * the memory manager. 
 */
void mman_destroy(mman_t *mman);

/**
 * @brief Initialize a memory manager node.
 *
 * Initialize a memory manager node. Nodes simply allocations
 * inside the memory manager. A node has to be initialized before
 * one can back it by an address region using mman_alloc.
 */
static inline void mman_node_init(mman_node_t *node) {
	rb_node_init(node, &node->node);
	rb_node_init(node, &node->freenode);
}

/**
 * @brief Destroy a memory manager node.
 */
static inline void mman_node_destroy(mman_node_t *node) {
	rb_node_destroy(&node->node);
	rb_node_destroy(&node->freenode);
}

/**
 * @brief Get the start address of a memory manager.
 */
static inline uint64_t mman_start(mman_t *mman) {
	return mman->node.addr;
}

/**
 * @brief Get the start address of a memory manager node.
 *
 * The return value is undefined when the node is not allocated
 * inside a memory manager.
 */
static inline uint64_t mman_node_addr(mman_node_t *node) {
	return node->addr;
}

/**
 * @brief Get the size of a memory manager node.
 *
 * The return value is undefined when the node is not allocated
 * inside a memory manager.
 */

static inline uint64_t mman_node_size(mman_node_t *node) {
	return node->size;
}

/**
 * @brief Get the end address of a memory manager node.
 *
 * The return value is undefined when the node is not allocated
 * inside a memory manager.
 */
static inline uint64_t mman_node_end(mman_node_t *node) {
	return node->addr - 1 + node->size;
}

/**
 * @brief Allocate an address region.
 *
 * Allocate an address region from a memory manager. On success, the
 * allocated address region can be obtained using mman_node_addr and
 * mman_node_end.
 *
 * @param mman The memory manager.
 * @param size The size of the allocation.
 * @param align The desired alignment of the allocation.
 * @param node The memory manager node describing the allocation.
 *
 * @retval 0 		Sucess.
 * @retval -ENOMEM	No free region was sound which could fullfill the
 *			request.
 */
int mman_alloc(mman_t *mman, uint64_t size, uint64_t align, mman_node_t *node);

/**
 * @brief Free an address region.
 *
 * Free an address region of a memory manager node. The node itself is not
 * freed and can be reused for subsequent mman_alloc or mman_insert.
 */
void mman_free(mman_t *mman, mman_node_t *node);

/**
 * @brief Insert a memory manager node inside a specific address region.
 *
 * Insert a memory manager node inside a specific address region. No allocation
 * may reside inside the memory region.
 *
 * @param mman The memory manager.
 * @param size The address of the address region.
 * @param align The size of the address region.
 * @param node The memory manager node describing the allocation.
 */
void mman_insert(mman_t *mman, uint64_t addr, uint64_t size,
	mman_node_t *node);

/**
 * @brief Free the last part of an allocation.
 *
 * @param mman The memory manager.
 * @param node An allocation from the memory mnager.
 * @param size The size to be freed at the end of the node.
 */
void mman_node_free_tail(mman_t *mman, mman_node_t *node, uint64_t size);

/**
 * @brief Free the first part of an allocation.
 *
 * @param mman The memory manager.
 * @param node An allocation from the memory mnager.
 * @param size The size to be freed at the beginning of the node.
 */
void mman_node_free_head(mman_t *mman, mman_node_t *node, uint64_t size);

/**
 * Lookup which allocation contains a specific address inside a memory manager.
 *
 * @param mman The memory manager.
 * @param add The address inside the memory manager.
 * @return	NULL No node was found at that address.
 *		else The pointer to the node.
 */
mman_node_t *mman_lookup(mman_t *mman, uint64_t addr);

/**
 * Lookup the first allocation inside a specific region inside a memory manager.
 *
 * @param mman The memory manager.
 * @param addr The start of the memory region.
 * @param size The size of the memory region.
 * * @return	NULL No allocation was found in that region.
 *		else The pointer to the first node in the region.
 */
mman_node_t *mman_first_node(mman_t *mman, uint64_t addr, uint64_t size);

/**
 * @brief Get the node next node.
 */
static inline mman_node_t *mman_node_next(mman_node_t *node) {
	return rb_next(&node->node);
}

/**
 * @brief Get the node next node.
 */
static inline mman_node_t *mman_node_prev(mman_node_t *node) {
	return rb_prev(&node->node);
}

/**
 * @brief Print the allocated and free regions of an allocator.
 */
void mman_debug(mman_t *mman);

#endif
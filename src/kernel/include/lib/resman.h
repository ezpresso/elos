#ifndef LIB_RESMAN_H
#define LIB_RESMAN_H

#include <lib/list.h>

/*
 * Generic and simple resource manager / allocator.
 */

typedef uint64_t resman_addr_t;
typedef uint64_t resman_size_t;
typedef uint64_t resman_off_t;

typedef struct resman {
	struct resman *parent;
	list_node_t node; /* node for parent->children */
	list_t children; /* list is not locked */
	resman_addr_t addr;
	resman_size_t size;

	/* list_t free;
	list_node_t free_node; */
} resman_t;

static inline resman_size_t resman_get_size(resman_t *resman) {
	return resman->size;
}

static inline resman_addr_t resman_get_addr(resman_t *resman) {
	return resman->addr;
}

static inline resman_addr_t resman_get_end(resman_t *resman) {
	return resman_get_addr(resman) + resman_get_size(resman) - 1;
}

/**
 * @brief Initialize a root resource manager
 * @param addr 	the start address for resource
 * @paran end 	the end of the resource
 */
void resman_init_root(resman_t *resman, resman_addr_t addr, resman_addr_t end);

/**
 * @brief Destroy a root resource manager
 */
void resman_destroy_root(resman_t *resman);

/**
 * @brief Allocate a resource in the parent resource region 
 * @param parent 	the parent resource
 * @param result 	the resulting resource will be stored in this structure
 * @param start 	the minimum start address of the resulting resource
 * @param end 		the highest address address in the resulting resource
 *			may be equal but not higher than _end_
 * @param size 		the desired size of the resources
 * @param align 	the alignment of the start address of the resulting
 *			region 
 * @return 		0 on success
 *			-ENOSPC if no space available for resource
 */
int resman_alloc_range(resman_t *parent, resman_t *result, resman_addr_t start,
	resman_addr_t end, resman_size_t size, resman_size_t align);
/**
 * @brief	Allocate a resource in the parent resource region, without any
 *		address restriction
 * @see 	resman_alloc
 * @return 0 	on success
 *		-ENOSPC if no space available for resource
 */
int resman_alloc(resman_t *parent, resman_t *result, resman_size_t size,
	resman_size_t align);

/**
 * @brief Shrinks an resman. Panics if size is greater than the resman's size
 * @return 0	on success
 *		-ENOTSUPP if there are child resmans preventing the shrinkng 
 */
int resman_shrink(resman_t *rm, resman_size_t size);

/**
 * @brief 	Reserves a region (i.e. _resman_alloc_ won't return an region
 *		overlapping this) inside the parent's region
 * @param parent
 * @paran rsvd 		the pointer to the structure where the reserved region
 *			is stored
 * @param start 	the start address of the reserved region
 * @param end 		the end of the reserved region
 * @return 		0 on success
 *			-ENOSPC if there is already an allocation between
 *			_start_ and _end_
 */
int resman_reserve(resman_t *parent, resman_t *rsvd, resman_addr_t start,
	resman_addr_t end);

/**
 * @brief Search for the resman including an address in its range
 */
resman_t *resman_lookup(resman_t *parent, resman_addr_t addr);

/**
 * @brief 	Free a resource allocated using resman_alloc/resman_alloc_range
 *		(and even resman_reserve)
 * @param resman  the resource to free (does not free the resman structure!)
 */
void resman_free(resman_t *resman);

void resman_print(resman_t *resman);

#endif
#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <kern/atomic.h>
#include <lib/list.h>
#include <vm/_pghash.h>

struct vm_object;

#define VM_PG_STATE_MASK 	0xf
#define 	VM_PG_FREE	0
#define 	VM_PG_NORMAL	1 /* normal kernel page, no pageout */
#define 	VM_PG_SLAB	2 /* used for slab */
#define 	VM_PG_MALLOC	3 /* used for big kmalloc allocations */
#define 	VM_PG_PGOUT	4 /* normal page, 'pageoutable' */
#define 	VM_PG_INACTIVE	5 /* considered inactive by pageout */
#define 	VM_PG_PINNED 	6 /* pincnt > 0, currently not 'pageoutable' */
#define 	VM_PG_LAUNDRY	7 /* currently being written to disk */
#define		VM_PG_SYNCQ	8
#define		VM_PG_SYNC	9 /* kinda same as VM_PG_LAUNDRY, but handling
				   * is a little bit different after completion.
				   */
#define VM_PG_DIRTY	(1 << 4)
#define VM_PG_BUSY	(1 << 5)
#define VM_PG_ERR	(1 << 6)
#define VM_PG_DEALLOC	(1 << 7)
#define VM_PG_LOCKED	(1 << 8)

/**
 * @brief Convert a page hash node into a page.
 */
#define PGH2PAGE(pgh_node) container_of(pgh_node, vm_page_t, node)

typedef uint16_t vm_pgflags_t;
typedef uint8_t vm_pgstate_t;

#if 0
typedef struct vm_pageq {
	sync_t lock;
	list_t list;
} vm_pageq_t;
void vm_pageq_page_add(vm_pageq_t *pageq, vm_page_t *page) {
	vm_pageq_idx_t idx = vm_pageq_idx(pageq);
	sync_assert(&pageq->lock);
	vm_page_lock(page);
}
#endif

typedef struct vm_page {
	vm_pghash_node_t node;

	union {
		struct vm_slab *slab;

		/*
		 * used for big allocations done by kmalloc
		 */
		vm_vsize_t malloc_sz;
		struct {
			list_node_t pgout_node;
			list_node_t obj_node;
			uint8_t syncq_idx;
		};
	};

	vm_pgflags_t flags;
	uint8_t order;
	uint16_t pincnt;
	uint8_t seg; /* seg only needs 7 bits TODO OR LESS */
} vm_page_t;

/**
 * @brief Assert that a page is pinned.
 */
#define vm_page_assert_pinned(page) ({		\
	assert(vm_page_pincnt(page) > 0);	\
})

/**
 * @brief Assert that a page is not pinned.
 */
#define vm_page_assert_not_pinned(page) ({	\
	assert(vm_page_pincnt(page) == 0);	\
})

/**
 * @brief Assert that a page is not busy.
 */
#define vm_page_assert_dirty(page) ({				\
	assert(vm_page_flag_test(page, VM_PG_DIRTY) == true);	\
})

/**
 * @brief Assert that a page is not busy.
 */
#define vm_page_assert_busy(page) ({				\
	assert(vm_page_flag_test(page, VM_PG_BUSY) == true);	\
})

/**
 * @brief Assert that a page is not busy.
 */
#define vm_page_assert_not_busy(page) ({			\
	assert(vm_page_flag_test(page, VM_PG_BUSY) == false);	\
})

/**
 * @brief Assert that a page is not free.
 */
#define vm_page_assert_allocated(page) ({	\
	assert(!vm_page_is_free(page));		\
})

static inline void vm_page_init(vm_page_t *page, uint8_t segid) {
	vm_pghash_node_init(&page->node);
	page->pincnt = 0;
	page->order = 0;
	page->seg = segid;
	page->flags = VM_PG_NORMAL;
}

static inline vm_pgflags_t vm_page_flags(vm_page_t *page) {
	return atomic_load_relaxed(&page->flags);
}

static inline vm_pgflags_t vm_page_flag_set(vm_page_t *page,
	vm_pgflags_t flags)
{
	return atomic_or_relaxed(&page->flags, flags);
}

static inline vm_pgflags_t vm_page_flag_clear(vm_page_t *page,
	vm_pgflags_t flags)
{
	return atomic_and_relaxed(&page->flags, ~flags);
}

static inline bool vm_page_flag_test(vm_page_t *page, vm_pgflags_t flags) {
	return (atomic_load_relaxed(&page->flags) & flags) == flags;
}

static inline void vm_page_set_state(vm_page_t *page, vm_pgstate_t state) {
	kassert((state & ~VM_PG_STATE_MASK) == 0, "[vm] page: invalid state: "
		"%u", state);

	/* The state of a page may need external protection, depending on
	 * how it is used.
	 */
	atomic_and_relaxed(&page->flags, ~VM_PG_STATE_MASK);
	atomic_or_relaxed(&page->flags, state);
}

static inline vm_pgstate_t vm_page_state(vm_page_t *page) {
	return atomic_load_relaxed(&page->flags) & VM_PG_STATE_MASK;
}

/**
 * @brief Can the page be used by pgout (or is pincnt ignored)?
 */
static inline bool vm_page_pageout_p(vm_page_t *page) {
	vm_pgstate_t state = vm_page_state(page);
	return state == VM_PG_PGOUT || state == VM_PG_INACTIVE ||
		state == VM_PG_PINNED || state == VM_PG_LAUNDRY ||
		state == VM_PG_SYNCQ || state == VM_PG_SYNC;
}

static inline bool vm_page_is_free(vm_page_t *page) {
	return vm_page_state(page) == VM_PG_FREE;
}

static inline struct vm_object *vm_page_object(vm_page_t *page) {
	return vm_pghash_object(&page->node);
}

static inline vm_objoff_t vm_page_offset(vm_page_t *page) {
	return vm_pghash_offset(&page->node);
}

void vm_page_pin(vm_page_t *page);
uint16_t vm_page_unpin(vm_page_t *page);

static inline uint16_t vm_page_pincnt(vm_page_t *page) {
	return atomic_load_relaxed(&page->pincnt);
}

void vm_page_dirty(vm_page_t *page);

static inline void vm_page_clean(vm_page_t *page) {
	vm_page_flag_clear(page, VM_PG_DIRTY);
}

static inline bool vm_page_is_dirty(vm_page_t *page) {
	return vm_page_flag_test(page, VM_PG_DIRTY);
}

static inline bool vm_page_is_busy(vm_page_t *page) {
	return vm_page_flag_test(page, VM_PG_BUSY);
}

/*
 * The page lock currently only protects page->object
 * TODO ...
 */
bool vm_page_trylock(vm_page_t *page);
void vm_page_lock(vm_page_t *page);
void vm_page_unlock(vm_page_t *page);
bool vm_page_lock_wait(vm_page_t *page);

void vm_page_busy(vm_page_t *page);
void vm_page_unbusy(vm_page_t *page);

/**
 * @brief Wait until a page is not busy anymore.
 * @retval false	The page was busy. If this happens the caller should
 *			unpin the page asap and not use the page in any other
 *			way. This is because an I/O-error might have happened
 *			while filling the page.
 * @retval true		Success. The page is not busy and thus may be used.
 */
bool vm_page_busy_wait(vm_page_t *page);

void vm_page_error(vm_page_t *page);

void vm_page_zero(vm_page_t *page);
void vm_page_zero_range(vm_page_t *page, size_t off, size_t size);

/**
 * Defined by arch. TODO doc
 */
void vm_page_cpy_partial(vm_page_t *dst, vm_page_t *src, size_t size);

static inline void vm_page_cpy(vm_page_t *dst, vm_page_t *src) {
	vm_page_cpy_partial(dst, src, PAGE_SZ);	
}

void vm_page_unmap(struct vm_object *object, vm_page_t *page);

/**
 * The caller has to make sure the page is safe to use (i.e. won't be freed).
 */
struct vm_object *vm_page_lock_object(vm_page_t *page);

#endif
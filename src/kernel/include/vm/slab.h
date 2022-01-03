#ifndef VM_SLAB_H
#define VM_SLAB_H

#include <kern/sync.h>
#include <kern/section.h>
#include <lib/list.h>
#include <vm/flags.h>

#define VM_SLAB_NOVALLOC VM_FLAG1 /* used by vmem */
#define VM_SLAB_SECTION section(slab, vm_slaballoc_t)

typedef struct vm_slaballoc {
	const char *name;
	size_t obj_size;
	size_t align;

	sync_t lock;

	/**
	 * List of free or partial free slabs. Completely free slabs
	 * are put at the end of the list and the others at the front.
	 */
	list_t free;

	/**
	 * The list node for the global slab allocator list.
	 */
	list_node_t node;
} vm_slaballoc_t;

#if 0
typedef struct vm_slab {
	vm_slaballoc_t *alloc;
	list_node_t node; /* node for alloc->free */
	struct vm_freeobj *free;
	void *ptr;
	size_t nfree;
	size_t nobj;
} vm_slab_t;
#endif

/* .free = LIST_FIELDS(&_name.free) */ \

/**
 * @brief Statically define a new slab allocator.
 */
#define DEFINE_VM_SLAB(_name, _size, _align) \
	section_entry(VM_SLAB_SECTION) vm_slaballoc_t _name = { \
		.name = #_name,			\
		.obj_size = _size,		\
		.align = _align,		\
		.lock = __SYNC_INIT(MUTEX),	\
		.free = __LIST_FIELDS_INIT(&_name.free) \
	}

/**
 * @brief Initialize the slab subsystem.
 */
void vm_slab_init(void);

/**
 * @brief Initialize a slab allocator used for allocationg memory of a given
 *	  size.
 *
 * @param slab	A pointer to a structure describing the allocator being created
 * @param name	A string describing the type of allocations being done
 * @param size 	The size of the objects, which can be allocated using this
 *				allocator
 * @param align The minimum alignment of the objects allocated using this
 *				allocator
 */
void vm_slab_create(vm_slaballoc_t *slab, const char *name, size_t size,
	size_t align);

/**
 * @brief Counterpart of vm_slab_create.
 */
void vm_slab_destroy(vm_slaballoc_t *slab);

/**
 * @brief Allocate memory from a slab allocator.
 * @param flags A combination of VM_SLAB_NOVALLOC, VM_ZERO and VM_WAIT
 */
void *vm_slab_alloc(vm_slaballoc_t *alloc, vm_flags_t flags);

/**
 * @brief Free an allocation from a slab allocator.
 */
void vm_slab_free(vm_slaballoc_t *alloc, void *ptr);

struct vm_slab;
/**
 * @brief Get the allocator structure from a slab.
 *
 * This function is internally used by kmalloc.
 */
vm_slaballoc_t *vm_slab_get_alloc(struct vm_slab *slab);

/**
 * This functions is internally used for implementing vmem_alloc. The problem
 * is that vmem_free needs
 */
void vm_slab_add_mem(vm_slaballoc_t *alloc, void *ptr, size_t size);

#endif

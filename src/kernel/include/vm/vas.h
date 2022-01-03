#ifndef VM_SPACE_H
#define VM_SPACE_H

#include <kern/rwlock.h>
#include <kern/sync.h>
#include <kern/cpu.h>
#include <vm/flags.h>
#include <vm/mmu.h>
#include <lib/mman.h>
#include <lib/list.h>

struct vm_object;
struct vm_map;

#define VM_MAP_SHARED		VM_FLAG1
#define VM_MAP_FIXED		VM_FLAG2
#define VM_MAP_PGOUT		VM_FLAG3
#define VM_MAP_32		VM_FLAG4
#define VM_MAP_SHADOW		VM_FLAG5

/*
 * The size of a mapping may be unaligned if this flag is set. When
 * the page is allocated, the remaining bits of the last page are
 * simply zeroed.
 */
#define VM_MAP_UNALIGNED	VM_FLAG6

#define VM_MAP_SHARED_P(f)	!!((f) & VM_MAP_SHARED)
#define VM_MAP_PRIV_P(f)  	!((f) & VM_MAP_SHARED)
#define VM_MAP_SHADOW_P(f)  	!!((f) & VM_MAP_SHADOW)

#define VM_MAP_ANY 0

#define vm_vas_current (cur_cpu()->vm_vas)

typedef struct vm_vas_funcs {
	/**
	 * Allocate a virtual address for a mapping.
	 *
	 * @retval -ENOMEM 	No space left in the virtual address space.
	 * @retval 0		Sucess.
	 */
	int (*map) (struct vm_vas *, vm_vsize_t size, struct vm_map *map);
	void (*map_fixed) (struct vm_vas *, vm_vaddr_t addr, vm_vsize_t size,
		struct vm_map *map);

	/**
	 * This callback is used for shrinking a map. The region that is being
	 * unmapped is described by @p addr and @p size.
	 */
	void (*unmap) (struct vm_vas *, vm_vaddr_t addr, vm_vsize_t size);
} vm_vas_funcs_t;

/**
 * @brief A virtual address space.
 */
typedef struct vm_vas {
	/*
	 * Architecture specific mmu context.
	 */
	mmu_ctx_t mmu;
	vm_vas_funcs_t *funcs;

	/*
	 * rwlock protecting the rb-tree
	 */
	rwlock_t lock;
	mman_t mman;
	vm_vaddr_t end;
} vm_vas_t;

#define MMAN2VM(node) container_of(node, vm_map_t, node)
typedef struct vm_map {
	vm_vas_t *vas;
	sync_t lock;
	mman_node_t node;
	vm_vaddr_t real_size; /**< The unaligned size of the mapping. */
	vm_flags_t flags;
	vm_flags_t max_prot;

	/*
	 * The offset into the object.
	 */
	vm_objoff_t offset;

	/*
	 * The object that is being mapped. However if the VM_MAP_SHADOW flag is
	 * set in map->flags, this object represents the object that has
	 * to be shadowed (i.e. on demand shadow object creation). vm_vas_lookup
	 * actually takes care of allocating the shadow object.
	 */
	struct vm_object *object;

	/*
	 * The node used for the list of mappings of an object. Remember that
	 * only the shadow-root actually has this list.
	 */
	list_node_t obj_node;
} vm_map_t;

/**
 * The kernel virtual address space. It behaves a little bit different than
 * a regular user virtual address space. One thing this address space does not
 * support is the partial unmapping of a vm_map_t. Furthermore the kernel vas
 * does not support fixed mappings.
 */
extern vm_vas_t vm_kern_vas;

static inline vm_vaddr_t vm_region_end(vm_vaddr_t start, vm_vsize_t size) {
	return start - 1 + size;
}

/**
 * @retval The starting address of a virtual address space.
 */
static inline vm_vaddr_t vm_vas_start(vm_vas_t *vas) {
	return mman_start(&vas->mman);
}

/**
 * @retval The end address of a virtual address space.
 */
static inline vm_vaddr_t vm_vas_end(vm_vas_t *vas) {
	return vas->end;
}

/**
 * @retval The size of a virtual address space.
 */
static inline vm_vsize_t vm_vas_size(vm_vas_t *vas) {
	return vas->end - vm_vas_start(vas) + 1;
}

static inline vm_vaddr_t vm_map_addr(vm_map_t *map) {
	return mman_node_addr(&map->node);
}

static inline vm_vsize_t vm_map_size(vm_map_t *map) {
	return mman_node_size(&map->node);
}

/**
 * @retval The end address (addr + size - 1) of a mapping.
 */
static inline vm_vaddr_t vm_map_end(vm_map_t *map) {
	return mman_node_end(&map->node);
}

/**
 * @retval The next mapping in the address sorted mapping list.
 */
static inline vm_map_t *vm_map_next(vm_map_t *map) {
	mman_node_t *node = mman_node_next(&map->node);
	return node ? MMAN2VM(node) : NULL;
}

/**
 * @retval The previous mapping in the address sorted mapping list.
 */
static inline vm_map_t *vm_map_prev(vm_map_t *map) {
	mman_node_t *node = mman_node_prev(&map->node);
	return node ? MMAN2VM(node) : NULL;
}

/**
 * Check if the access described by @p flags is granted on a mapping.
 * @param map 	The mapping being accessed.
 * @param flags Any combination of VM_PROT_RD, VM_PROT_WR, VM_PROT_EXEC,
 *		VM_PROT_USER and VM_PROT_KERN. VM_PROT_USER and VM_PROT_KERN
 *		may however not be used simultaniously.
 * @retval true Permission is granted.
 * @retval false Permission is denied.
 */
static inline bool vm_map_perm(vm_map_t *map, vm_flags_t flags) {
	return (VM_PROT_WR_P(map->flags) || !VM_PROT_WR_P(flags)) &&
		(VM_PROT_RD_P(map->flags) || !VM_PROT_RD_P(flags)) &&
		(!VM_PROT_EXEC_P(flags) || VM_PROT_EXEC_P(map->flags)) &&
		(VM_PROT_USER_P(map->flags) || VM_PROT_KERN_P(flags));
}

/**
 * @brief Initialize a virtual address space structure.
 */
void vm_vas_init(vm_vas_t *vas, vm_vaddr_t start, vm_vaddr_t end,
	vm_vas_funcs_t *funcs);

/**
 * @brief Deinitialize a virtual address space structure.
 */
void vm_vas_destroy(vm_vas_t *vas);

/**
 * @brief Map an object into a virtual address space.
 *
 * @param vas		The virtual address space.
 * @param addr		The page aligned address at which the mapping should be
 *			placed. This argument is only useful for fixed
 *			mappings.
 * @param size		The page aligned size of the desired mapping.
 * @param object	The object that should be mapped into the address space.
 * @param off		The page aligned offset into the object.
 * @param flags		Supported flags are VM_PROT_RD, VM_PROT_WR,
 *			VM_PROT_KERN, VM_PROT_USER, VM_PROT_EXEC, VM_MAP_SHARED,
 *			VM_MAP_FIXED, VM_MAP_PGOUT, VM_MAP_32, VM_MAP_SHADOW.
 * TODO DOC MAX_PROT
 * @param[out] out	The address at which the mapping was placed. This is the
 *			same as @addr if a fixed mapping succedes.
 *
 * @retval 0		Success.
 * @retval -EINVAL	@p addr, @p size or @p off were not page aligned.
 * @retval -EINVAL	@p addr and @p size are not in the range of the virtual
 *			address space for a fixed mapping.
 * @retval -ENOMEM	There is no usable virtual memory region left inside the
 *			virtual address space.
 */
int vm_vas_map(vm_vas_t *vas, vm_vaddr_t addr, vm_vsize_t size,
	struct vm_object *object, vm_objoff_t off, vm_flags_t flags,
	vm_flags_t max_prot, void **out);

/**
 * @brief Unmap a region in an virtual address space.
 * @retval 0 		Success.
 * @retval -EINVAL	@p addr and @p size are not in the range of the virtual
 *			address space.
 */
int vm_vas_unmap(vm_vas_t *vas, vm_vaddr_t addr, vm_vsize_t size);


int vm_vas_protect(vm_vas_t *vas, vm_vaddr_t addr, vm_vsize_t size,
		vm_flags_t flags);

/**
 * @brief Fork the current virtual address space.
 */
void vm_vas_fork(vm_vas_t *dst, vm_vas_t *src);

/**
 * Lookup a mapping inside a virtual address space. Since the mapping has
 * to be protected from unmapping, it's the caller's task to call
 * vm_vas_lookup_done(map) to indicate that it no longer uses the mapping.
 *
 * @retval 0		Success.
 * @retval -ENOENT	No mapping found at @p addr.
 */
int vm_vas_lookup(vm_vas_t *vas, vm_vaddr_t addr, vm_map_t **mapp);

/**
 * @see vm_vas_lookup.
 */
static inline void vm_vas_lookup_done(vm_map_t *map) {
	sync_release(&map->lock);
}

/**
 * Similar to vm_vas_lookup, but customized for page-faults.
 * Do not use this function.
 */
int vm_vas_fault(vm_vas_t *vas, vm_vaddr_t addr, vm_flags_t access,
	vm_map_t **mapp, struct vm_object **objectp);

/**
 * Do not use this function.
 */
static inline void vm_vas_fault_done(vm_map_t *map) {
	sync_release(&map->lock);
}

/**
 * @brief Get the object offset from a virtual address inside a mapping.
 */
static inline vm_objoff_t vm_map_addr_offset(vm_map_t *map, vm_vaddr_t addr) {
	return addr - vm_map_addr(map) + map->offset;
}

/**
 * @brief Get the virtual address from an object offset from a mapping.
 */
static inline vm_vaddr_t vm_map_offset_addr(vm_map_t *map, vm_objoff_t offset) {
	return vm_map_addr(map) + (offset - map->offset);
}

/**
 * @brief Allocate a virtual address space for a user-mode process.
 */
vm_vas_t *vm_user_vas_alloc(void);

/**
 * @brief Free a user virtual address space.
 */
void vm_user_vas_free(vm_vas_t *vas);

/**
 * @brief Switch the virtual address space of the current processor.
 */
void vm_vas_switch(vm_vas_t *vas);

/**
 * @brief Print some info about a virtual address space.
 */
void vm_vas_debug(vm_vas_t *vas);

#endif

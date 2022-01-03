#ifndef VM_OBJECT_H
#define VM_OBJECT_H

#include <kern/atomic.h>
#include <kern/sync.h>
#include <lib/list.h>
#include <vm/flags.h>

struct vm_map;
struct vm_object;
struct vm_page;
struct vm_pghash_node;
struct vm_pager_t;

#define VM_IS_VNODE(obj) ((obj)->ops == &vm_vnode_ops)
#define VM_IS_ANON(obj)	((obj)->ops == &vm_anon_ops)
#define VM_IS_SHDW(obj)	((obj)->ops == &vm_shadow_ops)
#define VM_IS_DEAD(obj)	((obj)->ops == NULL)

typedef int (vm_obj_map_t) (struct vm_object *, struct vm_map *map);
typedef int (vm_obj_fault_t) (struct vm_object *, vm_objoff_t off,
		vm_flags_t flags, vm_flags_t *map_flags, struct vm_page **page);
typedef int (vm_obj_initpage_t) (struct vm_object *, struct vm_page *);
typedef void (vm_obj_destroy_t) (struct vm_object *);
typedef void (vm_obj_dirty_t) (struct vm_object *, struct vm_page *);

typedef struct vm_obj_ops {
	vm_obj_map_t		*map;
	vm_obj_fault_t		*fault;
	vm_obj_destroy_t	*destroy;
	vm_obj_initpage_t	*initpage;
	vm_obj_dirty_t		*dirty;
} vm_obj_ops_t;

typedef struct vm_object {
	vm_obj_ops_t *ops;
	struct vm_pager *pager;

	sync_t lock;
	ref_t ref;
	list_t pages;

	/*
	 * Remember that the size of an object does not need to
	 * be aligned.
	 */
	vm_objoff_t size;

	/*
	 * Only the root objects store the list of mappings. This list
	 * contains the maps for every object in the shadow chain.
	 */
	union {
		list_t maps;
		struct vm_object *root;
	};
} vm_object_t;

extern vm_obj_ops_t vm_vnode_ops;
extern vm_obj_ops_t vm_shadow_ops;
extern vm_obj_ops_t vm_anon_ops;

vm_obj_fault_t vm_generic_fault;

/**
 * @brief Initialize a virtual memory object.
 *
 * @param object	The object to be initialized.
 * @param size		The initial size of the object (not necesserily page
 *			aligned).
 * @param ops		The callbacks associated with the object.
 * @param pager		The pager of the object. If NULL the default pager
 *			will be used.
 */
void vm_object_init(vm_object_t *object, vm_objoff_t size,
	vm_obj_ops_t *ops, struct vm_pager *pager);

/**
 * @brief Destroys a virtual memory object.
 *
 * The caller must ensure that there is no further reference to this
 * object and that every page is removed from the object, which is done
 * by calling vm_object_clear().
 *
 * @param object The object to be destroyed.
 */
void vm_object_destroy(vm_object_t *object);

/**
 * @brief Allocate a new virtual memory object.
 *
 * @param size		The initial size of the object (not necesserily page
 *			aligned).
 * @param ops		The callbacks associated with the object.
 */
vm_object_t *vm_object_alloc(vm_objoff_t size, vm_obj_ops_t *ops);

/**
 * @brief Free a memory object.
 *
 * Free a memory object allocated using vm_object_alloc().
 *
 * @param object The object to be freed.
 */
void vm_object_free(vm_object_t *object);

/**
 * @brief Free any pages associated with a virtual memory object.
 *
 * This function removes every page of the object from the pageout queues
 * and frees them. The pages are not written to disk even if they are
 * dirty. The caller must hold the lock of the object.
 */
void vm_object_clear(vm_object_t *object);

/**
 * @brief Get the size of a virtual memory object.
 *
 * @param object 	The virtual memory object.
 * @retval 		The size of the object.
 */
static inline vm_objoff_t vm_object_size(vm_object_t *object) {
	sync_assert(&object->lock);
	return object->size;
}

/**
 * @brief Increment the reference counter of an object.
 *
 * @param object The virtual memory object.
 * @retval The pointer to @p object for convenience.
 */
static inline vm_object_t *vm_object_ref(vm_object_t *object) {
	ref_inc(&object->ref);
	return object;
}

/**
 * @brief Decrement the reference counter of an object.
 *
 * Decrement the reference counter of an object. Whem the object
 * reaches a reference-count of zero, the destroy callback of
 * the object is called.
 *
 * @param object The virtual memory object.
 */
static inline void vm_object_unref(vm_object_t *object) {
	if(ref_dec(&object->ref)) {
		/*
		 * The reference count of the object might be
		 * incremented while an object is being destroyed
		 * simply because the object still has pages which
		 * might be used by pageout. E.g. pageout uses
		 * vm_page_lock_object which might need to increment
		 * the reference count of the object.
		 */
		if(!VM_IS_DEAD(object)) {
			object->ops->destroy(object);
		}
	}
}

/**
 * @brief Mark the object as being dead.
 *
 * Mark the object as being dead by setting object->ops to NULL. The
 * caller must hold the lock of the object.
 *
 * @param object The virtual memory object.
 */
static inline void vm_object_dead(vm_object_t *object) {
	sync_assert(&object->lock);
	kassert(!VM_IS_DEAD(object), "[vm] object: killing dead object");
	object->ops = NULL;
}

/**
 * @brief Get the root object of a shadow chain.
 *
 * Get the root object of a shadow chain. If the @p object is
 * no shadow object and this a shadow-root, @p object is retuned.
 *
 * @param object	The virtual memory object.
 * @retval 		The shadow root. If @p object is not a shadow object,
 *			@p object is returned. Remember that no new reference
 *			of the root object is returned and thus the caller
 *			should not call vm_object_unref with the returned
 *			object.
 */
static inline vm_object_t *vm_shadow_root(vm_object_t *object) {
	if(VM_IS_SHDW(object)) {
		return object->root;
	} else {
		return object;
	}
}

/**
 * @brief Register a new mapping for a virtual memory object.
 *
 * @param object	The virtual memory object.
 * @param map		The new mapping, mapping @p object.
 */
void vm_object_map_add(vm_object_t *object, struct vm_map *map);

/**
 * @brief Remove a mapping of a virtual memory object.
 *
 * Remove a mapping of a virtual memory object, added to the object
 * by a previous call to vm_object_map_add.
 *
 * @param object	The virtual memory object.
 * @param map 		The mapping to be removed
 */
void vm_object_map_rem(vm_object_t *object, struct vm_map *map);

/**
 * @brief Register a new mapping for a virtual memory object.
 *
 * This function is called right before mmaping. The mmap cannot
 * fail anymore after calling this function. It calls the optional
 * map callback of the object and adds the mapping tho the mapping
 * list of the object.
 *
 * @param object	The virtual memory object.
 * @param map		The new mapping, mapping @p object.
 */
int vm_object_map(vm_object_t *object, struct vm_map *map);

/**
 * @brief Allocate a new page for an object
 *
 * Allocate a new page for an object. This page is returned in a busy and
 * pinned state. The caller must hold the lock of @p object and verify
 * that the object does not have a page at @p off before calling this
 * function.
 *
 * @param object	The virtual memory object.
 * @param off		The offset of the page in the object. This value must
 *			be page aligned and smaller than the size of @p object.
 * @retval		A pointer to the freshly allocated page.
 */
struct vm_page *vm_object_page_alloc(vm_object_t *object, vm_objoff_t off);

/**
 * @brief Disassociate a page with its object.
 *
 * Remove the page from an object. Remember that the page is not being freed
 * it is just removed. The caller must hold the lock of @p object and
 * must ensure that the page is not on a pageout queue.
 *
 * @param object	The virtual memory object.
 * @param page 		The page to be removed from @p object.
 */
void vm_object_page_remove(vm_object_t *object, struct vm_page *page);

/**
 * @brief Lookup the page of an object at a specific offset.
 *
 * Lookup the page of an object at a specific offset. The caller must
 * hold the lock of @p object.
 *
 * @param object	The virtual memory object.
 * @param off		The offset in the object of the relevant page. Needs
 *			to be page aligned.
 * @param[out] out	The resulting page.
 * @retval 0		Success.
 * @retval -ERANGE	@p off is greater than the size of @p object.
 * @retval -ENOENT	No page was present at the specified offset.
 */
int vm_object_page_resident(vm_object_t *object, vm_objoff_t off,
	struct vm_page **out);

/**
 * @brief Inform the object that an error occured while filling a page.
 *
 * When a page-fault happens and a new page needs to be allocated,
 * the page is marked busy, until it is filled with data. Other threads
 * are waiting while a page is busy if they need the page. This allows
 * the page-fault code to unlock the object before filling the page.
 * However an error may occur while filling the page and this function
 * Informs the object that an error occured while filling one of its pages.
 * Other threads waiting for the busy flag to clear will be woken up, the
 * page will be removed from the object and the page is freed asap.
 * The caller must not hold the lock of @p object and the caller must be
 * the one trying to fill the page.
 *
 * @param object	The virtual memory object.
 * @param page 		The page.
 */
void vm_object_page_error(vm_object_t *object, struct vm_page *page);

/**
 * @brief Change the size of a virtual memory object.
 *
 * Change the size of a virtual memory object. If the size is decreased,
 * some parts of the object might need to be unmapped. The caller must
 * hold the lock of @p object.
 *
 * @param object	The virtual memory object.
 * @param size 		The new size of the object, which does not need to be
 *			page aligned.
 */
void vm_object_resize(vm_object_t *object, vm_objoff_t size);

/**
 * @brief Handle a page fault.
 *
 * This function looks up the page of the object at @p off. If the page
 * is not present, must of the time a new page is allocated and populated
 * with data. However if the caller only requests read access, the page
 * might be some sort of shared page. The caller must hold the lock of
 * @p object, but vm_object_fault returns @p object in an unlocked state.
 *
 * @param object	The virtual memory object.
 * @param off 		The offset in the memory object.
 * @param access 	The access that triggered the page fault. A combination
 *			of VM_PROT_READ and VM_PROT_WRITE.
 * @param[in,out] map_flags The caller eventually wants to map the resulting
 *			page and this field describes the desired flags of the
 *			new mapping. However if @p access states that only
 *			read access was requested, it is possible that the
 *			VM_PROT_WRITE bit is cleared from the mapping flags,
 *			eventhough the vm_map is considered writeable.
 * @param[out] page The resulting page.
 *
 * @retval 0		Success.
 *	   -ENOMEM	No page could be allocated due to memory
 *			shortage.
 *	   -ERANGE	This can happen a vnode was truncated somehow and
 *			now the vnode is smaller than @p off.
 */
int vm_object_fault(vm_object_t *object, vm_objoff_t off, vm_flags_t access,
		vm_flags_t *map_flags, struct vm_page **pg);

/**
 * @brief Move pages from one object to another one.
 *
 * Move all pages starting at @p off of the object @p src to the object
 * @p dst. The caller must hold the lock for both objects. This function
 * might occasionally have to unlok both objects. Shadow chaings need this
 * function for collapsing the chain. Due to the nature of shadow-chains and
 * how shadow chains are locked, @p dst is relocked before @p src in this
 * case. Furhtermore, if @p dst already contains a page and @p src contains
 * a page at the same offset, the page of @p src is simply freed.
 *
 * @brief dst The destination object.
 * @brief src The source object.
 * @brief off Pages lower than this offset will not be migrated.
 */
void vm_object_pages_migrate(vm_object_t *dst, vm_object_t *src,
	vm_objoff_t off);

/**
 * @brief Allocate an anonymous object.
 */
vm_object_t *vm_anon_alloc(vm_objoff_t size, vm_flags_t flags);

/**
 * @brief Register a demand shadow.
 *
 * When a fork happens, the shadow objects are allocated in a lazy fashion. Such
 * demand shadow object allocations are registered using this function.
 */
void vm_demand_shadow_register(vm_object_t *object);

/**
 * @brief Unregister a demand shadow.
 *
 * Unregister a lazy shadow previously registered via vm_demand_shadow_register
 * and never performed.
 */
void vm_demand_shadow_unregister(vm_object_t *object);

/**
 * @brief Perform a demand shadow.
 *
 * Actually perform a demand shadow, which was previously registered by
 * vm_demand_shadow_register. In some cases no new shadow object is needed
 * and thus a new reference to the same object might be returned. Do not try
 * to unregister the demand shadow afterwards.
 */
vm_object_t *vm_demand_shadow(vm_object_t *shadowed, vm_objoff_t size);

#endif

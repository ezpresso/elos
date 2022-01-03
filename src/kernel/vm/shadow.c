/*
 * ███████╗██╗	  ██████╗ ███████╗
 * ██╔════╝██║	 ██╔═══██╗██╔════╝
 * █████╗  ██║	 ██║   ██║███████╗
 * ██╔══╝  ██║	 ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 *
 * Copyright (c) 2018, Elias Zell
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <kern/system.h>
#include <vm/object.h>
#include <vm/page.h>
#include <vm/flags.h>
#include <vm/pghash.h>
#include <vm/mmu.h>
#include <vm/vm.h>
#include <vm/slab.h>

#define VM_OBJ_TO_SHDW(obj) container_of(obj, vm_shadow_t, object)
typedef struct vm_shadow {
	vm_object_t object;
	vm_object_t *shadow;
	list_node_t node;
	list_t shdw_list;
	size_t depth;
	size_t demand_shadow;
} vm_shadow_t;

static DEFINE_VM_SLAB(vm_shadow_slab, sizeof(vm_shadow_t), 0);
static vm_obj_fault_t	vm_shadow_fault;
static vm_obj_destroy_t	vm_shadow_destroy;
vm_obj_ops_t vm_shadow_ops = {
	.fault =	vm_shadow_fault,
	.destroy =	vm_shadow_destroy,
};

static vm_page_t *vm_shadow_object_page(vm_object_t *object, vm_objoff_t off) {
	vm_page_t *page;
	int retv;

	retv = vm_object_page_resident(object, off, &page);
	if(retv != 0) {
		/*
		 * -ERANGE aka EOF is currently not possible, since none of the
		 * shadow objects in a shadow chain are smaller than
		 * their shadow (=> the smallest shadow objects may only be
		 * leaf nodes of the shadow tree)
		 */
		assert(retv != -ERANGE);
		return NULL;
	} else {
		return page;
	}
}

/**
 * @brief Walk through the shadown page until a the requested page is found.
 */
static int vm_shadow_chain_get(vm_object_t *object, vm_objoff_t off,
	vm_page_t **pagep)
{
	vm_object_t *cur, *root, *next;
	vm_page_t *page = NULL;
	int err;

	/*
	 * Look through the shadow chain.
	 */
	cur = vm_object_ref(VM_OBJ_TO_SHDW(object)->shadow);
	root = vm_shadow_root(object);
	kassert(root != object, NULL);

	/*
	 * Avoid recursion here, because the kernel stack is rather small.
	 */
	while(cur != root) {
		/*
		 * Every object in the shadow chain except the root
		 * object has to be a shadow object.
		 */
		kassert(VM_IS_SHDW(cur), NULL);
		sync_acquire(&cur->lock);

		/*
		 * Look if the page is resident in the object.
		 */
		page = vm_shadow_object_page(cur, off);
		if(page != NULL) {
			sync_release(&cur->lock);
			break;
		}

		/*
		 * Continue with the next object in the shadow chain.
		 */
		next = vm_object_ref(VM_OBJ_TO_SHDW(cur)->shadow);

		sync_release(&cur->lock);
		vm_object_unref(cur);
		cur = next;
	}

	vm_object_unref(cur);

	/*
	 * If we didn't find the page in any of the shadow objects
	 * in the chain, get the page from the root-object.
	 */
	if(page == NULL) {
		vm_flags_t tmp = VM_PROT_RD;

		/*
		 * If none of the shadow objects in the shadow chain had the
		 * page resident, get the page from the shadow root.
		 */
		sync_acquire(&root->lock);
		err = vm_object_fault(root, off, tmp, &tmp, &page);
		sync_release(&root->lock);
	} else {
		err = 0;
	}

	assert(err || page != NULL);
	return *pagep = page, err;
}

static int vm_shadow_fault(vm_object_t *object, vm_objoff_t off,
	vm_flags_t access, vm_flags_t *map_flags, vm_page_t **pagep)
{
	vm_page_t *page = NULL;
	size_t cpy_size;
	int err;

	err = vm_shadow_chain_get(object, off, &page);
	if(err) {
		return err;
	}

	/*
	 * When shadowing a vnode object and the size of the shadow is
	 * page aligned and when only requesting read access the page
	 * does not need to be copied. However if the shadow size is
	 * not aligned, the last page has to be copyied eventhough
	 * read access was requested, because the rest of the page
	 * has to be zero filled. Since the sizes of shadow objects
	 * in a shadow chain are always the same, this extra copy
	 * only has to be done when copying from the shadow root (i.e.
	 * vnode object).
	 */
	if(VM_IS_SHDW(vm_page_object(page))) {
		cpy_size = PAGE_SZ;
	} else {
		cpy_size = min(vm_object_size(object) - off, PAGE_SZ);
	}

	/*
	 * Do 'copy-on-write', when write access was requested or
	 * parts of the new page have to be zeroed.
	 */
	if(VM_PROT_WR_P(access) || cpy_size != PAGE_SZ) {
		vm_page_t *new;

		/*
		 * This function returns the new page in a busy, a
		 * pinned state.
		 */
		new = vm_object_page_alloc(object, off);
		if(new == NULL) {
			vm_page_unpin(page);
			return -ENOMEM;
		}

		vm_page_dirty(new);
		sync_release(&object->lock);

		/*
		 * Do the copy. Handle object sizes which are not page aligned.
		 */
		vm_page_cpy_partial(new, page, cpy_size);

		/*
		 * Unpin the page which was copied.
		 */
		vm_page_unpin(page);

		sync_acquire(&object->lock);
		vm_page_unbusy(new);
		page = new;
	} else {
		/*
		 * Just a quick note when shadowing a vnode object and a page
		 * from the vnode is shared with the shadow object since the
		 * shadow only reads this page:
		 *
		 * "It is unspecified whether changes made to the file after
		 *	the  mmap() call are visible in the mapped region."
		 *
		 * => read-only private vnode pages may change content
		 * eventhough they are not shared.
		 */
		*map_flags &= ~VM_PROT_WR;
	}

	return *pagep = page, 0;
}

static void vm_shadow_init(vm_shadow_t *object, vm_object_t *shadowed,
	vm_objoff_t size)
{
	vm_object_init(&object->object, size, &vm_shadow_ops, NULL);
	list_node_init(object, &object->node);
	list_init(&object->shdw_list);

	/*
	 * Don't count 'root' as a reference.
	 */
	object->object.root = vm_shadow_root(shadowed);
	object->shadow = vm_object_ref(shadowed);
	object->demand_shadow = 0;
	object->depth = 1;
}

static bool vm_shadow_needed(vm_shadow_t *object) {
	sync_assert(&object->object.lock);
	assert(object->demand_shadow >= 1);

	/*
	 * If the object being shadowed is also a shadow object and we are
	 * be the only one, which can shadow it, a shadow is useless. In this
	 * case we can reuse the object itself.
	 */
	return object->demand_shadow != 1 || list_length(&object->shdw_list);
}

vm_object_t *vm_demand_shadow(vm_object_t *shadowed, vm_objoff_t size) {
	vm_shadow_t *object;

	/*
	 * In some cases we don't need to shadow.
	 */
	if (VM_IS_SHDW(shadowed)) {
		sync_scope_acquire(&shadowed->lock);
		if (!vm_shadow_needed(VM_OBJ_TO_SHDW(shadowed))) {
			VM_OBJ_TO_SHDW(shadowed)->demand_shadow--;
			return vm_object_ref(shadowed);
		}
	}

	object = vm_slab_alloc(&vm_shadow_slab, VM_WAIT);
	if (VM_IS_SHDW(shadowed)) {
		/*
		 * Now that we allocated a new object, we have to recheck
		 * whether we still need to actually shadow the object.
		 */
		sync_acquire(&shadowed->lock);
		if (!vm_shadow_needed(VM_OBJ_TO_SHDW(shadowed))) {
			VM_OBJ_TO_SHDW(shadowed)->demand_shadow--;
			sync_release(&shadowed->lock);
			vm_slab_free(&vm_shadow_slab, object);
			return vm_object_ref(shadowed);
		} else {
			vm_shadow_init(object, shadowed, size);
			object->depth = VM_OBJ_TO_SHDW(shadowed)->depth + 1;

			VM_OBJ_TO_SHDW(shadowed)->demand_shadow--;
			list_append(&VM_OBJ_TO_SHDW(shadowed)->shdw_list,
				&object->node);
			sync_release(&shadowed->lock);
		}
	} else {
		vm_shadow_init(object, shadowed, size);
	}

	return &object->object;
}

static bool vm_shadow_can_simplify(vm_shadow_t *object) {
	return list_length(&object->shdw_list) == 1 && !object->demand_shadow;
}

static void vm_shadow_simplify(vm_shadow_t *object) {
	vm_object_t *gparent;
	vm_shadow_t *child;

	sync_assert(&object->object.lock);
	if(!vm_shadow_can_simplify(object)) {
		sync_release(&object->object.lock);
		return;
	}

	child = list_first(&object->shdw_list);
	vm_object_ref(&child->object);

	/*
	 * The child has to be locked first, in order to adhere to
	 * the locking order in shadow chains. Thus the object
	 * has to be unlocked and locked again after locking the
	 * child.
	 */
	sync_release(&object->object.lock);

	synchronized(&child->object.lock) {
		sync_scope_acquire(&object->object.lock);

		/*
		 * The situation might have changd while messing around
		 * with the locks.
		 */
		if(!vm_shadow_can_simplify(object) ||
			list_first(&object->shdw_list) != child ||
			VM_IS_DEAD(&child->object))
		{
			goto out;
		}

		/*
		 * Move every page from parent to child. This is done before
		 * removing _parent_ from the shadow chain, because
		 * pages_migrate() might have to unlock both objects temporarily
		 * and the shadow chain should not be in a half finished state.
		 */
		vm_object_pages_migrate(&child->object, &object->object, 0);

		gparent = object->shadow;
		child->shadow = gparent;
		child->depth--;
		object->shadow = NULL;
		list_remove(&object->shdw_list, &child->node);

		if(VM_IS_SHDW(gparent)) {
			sync_scope_acquire(&gparent->lock);
			list_remove(&VM_OBJ_TO_SHDW(gparent)->shdw_list,
				&object->node);
			list_append(&VM_OBJ_TO_SHDW(gparent)->shdw_list,
				&child->node);
		}
	}

	/*
	 * Remove the reference from child->shadow.
	 * TODO the unref might recurse
	 *		=> either define a maximum shadow depth
	 *			or vm_object_unref_async();
	 */
	vm_object_unref(&object->object);

out:
	vm_object_unref(&child->object);
}

void vm_demand_shadow_register(vm_object_t *object) {
	if(VM_IS_SHDW(object)) {
		sync_scope_acquire(&object->lock);
		VM_OBJ_TO_SHDW(object)->demand_shadow++;
	}
}

void vm_demand_shadow_unregister(vm_object_t *object) {
	if(VM_IS_SHDW(object)) {
		vm_shadow_t *shdw = VM_OBJ_TO_SHDW(object);

		sync_acquire(&shdw->object.lock);
		shdw->demand_shadow--;

		/*
		 * simplify() unlocks object->lock.
		 */
		vm_shadow_simplify(shdw);
	}
}

static void vm_shadow_destroy(vm_object_t *object) {
	vm_shadow_t *shdw = VM_OBJ_TO_SHDW(object);

	synchronized(&object->lock) {
		vm_object_dead(object);
	}

	if(shdw->shadow != NULL && VM_IS_SHDW(shdw->shadow)) {
		vm_shadow_t *parent = VM_OBJ_TO_SHDW(shdw->shadow);

		sync_acquire(&parent->object.lock);

		/*
		 * If the parent object is also a shadow object,
		 * it might concurrently try to call vm_shadow_simplify()
		 * and thus try to remove itself from the shadow chain.
		 * The problem is that this object is still on the parent's
		 * list of shadow objects. In this case the parent may actually
		 * try to protect the child object by adding a reference to
		 * this object, unlocking itself, locking the child and locking
		 * itself again.
		 * When the parent is currently using this object again
		 * (even if it's just for a short time), we may not free
		 * the object. vm_shadow_simplify() will unref the object
		 * when it's done and thus another destroy() of this object
		 * will be attempted.
		 */
		if(ref_get(&object->ref)) {
			sync_release(&parent->object.lock);
			return;
		}

		/*
		 * Detach this object from the parent.
		 */
		list_remove(&parent->shdw_list, &shdw->node);

		/*
		 * Now that one shadow object of the parent was removed,
		 * check if the shadow chain can be simplified i.e. the parent
		 * (being a shadow object) is only shadowed one time and it
		 * can be mergeed with its child.
		 * Remember that this function unlocks the parent.
		 */
		vm_shadow_simplify(parent);
	}

	if(shdw->shadow) {
		/* TODO recursion */
		vm_object_unref(shdw->shadow);
	}

	list_node_destroy(&shdw->node);
	list_destroy(&shdw->shdw_list);
	synchronized(&object->lock) {
		vm_object_clear(object);
	}
	vm_object_destroy(object);
	vm_slab_free(&vm_shadow_slab, shdw);
}

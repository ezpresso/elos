/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 *
 * Copyright (c) 2017, Elias Zell
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
#include <kern/rwlock.h>
#include <kern/init.h>
#include <vm/page.h>
#include <vm/object.h>
#include <vm/phys.h>
#include <lib/hashtab.h>

/*
 * The size of the pghash is (physical memory >> VM_PGHASH_SHIFT)
 * If VM_PGHASH_SHIFT is 8 the size of pghash is:
 *	128MB RAM: 512kb
 *	4GB   RAM: 16MB
 */
#define VM_PGHASH_SHIFT	8

/*
 * A vm_pghash_node_t stores some information in the offset field, since
 * the offset is always page aligned and thus has some free bits to store
 * information.
 */
ASSERT(PAGE_SZ >= (VM_PGHASH_MASK + 1), "page size is too small");

static rwlock_t vm_pghlock = RWLOCK_INIT;
static hashtab_t vm_pghash;

static size_t vm_pghash_hash(vm_object_t *obj, vm_objoff_t off) {
	return ((uintptr_t)obj ^ off) & (vm_pghash.nentries - 1);
}

void vm_pghash_migrate(struct vm_object *old, vm_pghash_node_t *node,
	struct vm_object *new)
{
	vm_objoff_t offset = vm_pghash_offset(node);
	size_t ohash, nhash;

	kassert(vm_pghash_object(node) == old, "[vm] pghash: "
		"migrating node: owner mismatch");
	kassert(old != new, "[vm] pghash: migrating node: same object");

	node->object = new;

	/*
	 * The hash value of the node has changed very likely, so
	 * a rehash is probably needed.
	 */
	ohash = vm_pghash_hash(old, offset);
	nhash = vm_pghash_hash(new, offset);
	if(ohash != nhash) {
		wrlock_scope(&vm_pghlock);
		hashtab_rehash(&vm_pghash, ohash, nhash, &node->node);
	}
}

void vm_pghash_add(struct vm_object *obj, unsigned type, vm_objoff_t off,
	vm_pghash_node_t *node)
{
	size_t hash;

	sync_assert(&obj->lock);
	kassert(ALIGNED(off, PAGE_SZ), "[vm] pghash: adding node: "
		"invalid offset: 0x%llx", off);
	kassert((type & ~VM_PGHASH_MASK) == 0, "[vm] pghash: adding node: "
		"invalid node type: %u", type);

	node->offset = off | type;
	node->object = obj;

	hash = vm_pghash_hash(obj, off);

	wrlock_scope(&vm_pghlock);
	hashtab_set(&vm_pghash, hash, &node->node);
}

void vm_pghash_rem(struct vm_object *obj, vm_pghash_node_t *node) {
	size_t hash;

	sync_assert(&obj->lock);

	hash = vm_pghash_hash(obj, vm_pghash_offset(node));
	wrlocked(&vm_pghlock) {
		hashtab_remove(&vm_pghash, hash, &node->node);
	}

	node->object = NULL;
	node->offset = 0;
}

vm_pghash_node_t *vm_pghash_lookup(struct vm_object *obj, vm_objoff_t off) {
	vm_pghash_node_t *node;
	size_t hash;

	kassert(ALIGNED(off, PAGE_SZ), "[vm] pghash: lookup: invalid "
		"offset: 0x%llx", off);
	hash = vm_pghash_hash(obj, off);

	rdlock_scope(&vm_pghlock);
	hashtab_search(node, hash, &vm_pghash) {
		if(vm_pghash_object(node) == obj &&
			vm_pghash_offset(node) == off)
		{
			return node;
		}
	}

	return NULL;
}

void __init vm_pghash_init(void) {
	size_t size;

	size = (vm_phys_get_total() >> VM_PGHASH_SHIFT) / sizeof(list_t);

	hashtab_alloc(&vm_pghash, size, VM_WAIT);
	kprintf("[vm] pghash entries: %d\n", vm_pghash.nentries);
}

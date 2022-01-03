/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
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

static vm_obj_initpage_t vm_anon_initpage;
static vm_obj_destroy_t	vm_anon_destroy;
vm_obj_ops_t vm_anon_ops = {
	.fault = vm_generic_fault,
	.destroy = vm_anon_destroy,
	.initpage = vm_anon_initpage,
};

static int vm_anon_initpage(__unused vm_object_t *object, vm_page_t *page) {
	vm_page_zero(page);
	return 0;
}

vm_object_t *vm_anon_alloc(vm_objoff_t size, vm_flags_t flags) {
	/*
	 * TODO Maybe we could create non-zero-initialized anon objects in
	 * cases, but this seems to be a security concern...
	 */
	VM_FLAGS_CHECK(flags, VM_ZERO);
	if(!VM_ZERO_P(flags)) {
		assert(ALIGNED(size, PAGE_SZ));
	}

	return vm_object_alloc(size, &vm_anon_ops);
}

static void vm_anon_destroy(vm_object_t *object) {
	synchronized(&object->lock) {
		vm_object_dead(object);
		vm_object_clear(object);
	}

	list_destroy(&object->maps);
	vm_object_free(object);
}

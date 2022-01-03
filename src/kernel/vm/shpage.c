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
#include <kern/init.h>
#include <vm/page.h>
#include <vm/phys.h>
#include <vm/vmem.h>
#include <vm/object.h>
#include <vm/kern.h>
#include <vm/layout.h>
#include <vm/vas.h>

static vm_object_t *vm_shobject;
static vm_page_t *vm_shpage;
static void *vm_shmap;
static size_t vm_shpage_next = 0;

void *vm_shpage_alloc(size_t size, size_t align) {
	size_t off;

	vm_shpage_next = ALIGN(vm_shpage_next, align);
	off = vm_shpage_next;
	vm_shpage_next += size;

	if(vm_shpage_next > PAGE_SZ) {
		kpanic("[vm] shared page: no space left");
	}

	return vm_shmap + off;
}

vm_page_t *vm_shpage_get(void) {
	return vm_shpage;
}

vm_paddr_t vm_shpage_phys(void) {
	return vm_page_phys(vm_shpage);
}

vm_vaddr_t vm_shpage_addr(void *ptr) {
	return (ptr - vm_shmap) + VM_SHAREDPAGE;
}

int vm_shpage_map(vm_vas_t *vas) {
	return vm_vas_map(vas, VM_SHAREDPAGE, PAGE_SZ, vm_shobject, 0,
		VM_PROT_RD | VM_PROT_EXEC | VM_PROT_USER | VM_MAP_FIXED |
		VM_MAP_SHARED, VM_PROT_RD | VM_PROT_EXEC, NULL);
}

void __init vm_shpage_init(void) {
	/*
	 * A virtual memory object is needed, so that a process
	 * can map it using the default mmap way.
	 */
	vm_shobject = vm_anon_alloc(PAGE_SZ, VM_ZERO);

	/*
	 * Allocate the actual shared page.
	 */
	sync_acquire(&vm_shobject->lock);
	vm_shpage = vm_object_page_alloc(vm_shobject, 0);
	sync_release(&vm_shobject->lock);
	if(vm_shpage == NULL) {
		kpanic("[vm] shared page: could not allocate the shared page");
	}

	/*
	 * Initialize the contents of the page to zero and clear the busy
	 * flag. Cannot call vm_page_unbusy here, because it would eventually
	 * call kern_wake, which is not setup during this stage of
	 * initialization. Furhtermore do not unpin the page since
	 * pgout should never tamper with that page.
	 *
	 * TODO maybe consider initializing the shpage later
	 */
	vm_page_zero(vm_shpage);
	vm_page_flag_clear(vm_shpage, VM_PG_BUSY);

	/*
	 * The kernel needs access to the shared page, so map it into
	 * kernel space.
	 */
	vm_kern_map_page(vm_shpage, VM_PROT_RW | VM_WAIT, &vm_shmap);
}

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
#include <vm/pager.h>
#include <vm/object.h>
#include <vm/phys.h>
#include <vm/page.h>
#include <vm/pghash.h>

int vm_pager_pagein(vm_object_t *object, vm_objoff_t offset,
	vm_pghash_node_t *node, vm_page_t **pagep)
{
	vm_pager_t *pager = object->pager;
	vm_page_t *page;
	int err;

	sync_assert(&object->lock);
	if(node == NULL && F_ISSET(pager->flags, VM_PAGER_PGHASH)) {
		return -ENOENT;
	}

	/*
	 * Allocate a new page for the data which is on disk.
	 */
	page = vm_object_page_alloc(object, offset);
	if(page == NULL) {
		return -ENOMEM;
	}


	/*
	 * Read the contents of the page from memory (may
	 * be asynchronous).
	 */
	err = pager->pagein(object, node, page);
	if(err) {
		vm_object_page_error(object, page);
	}

#if notyet
	/*
	 * In case the pagein was asynchronous.
	 *
	 * TODO would this be needed
	 */
	if(vm_object_page_wait(object, page) == false) {
		return -EIO;
	} else
#endif

	return *pagep = page, err;
}

int vm_pager_pageout(vm_object_t *object, vm_page_t *page) {
	sync_assert(&object->lock);

	/*
	 * Unmap the page from every address space.
	 */
	vm_page_unmap(object, page);

	/*
	 * Make subsequent page-faults (+ additional operations
	 * like vm_object_resize) to wait until the pageout no
	 * longer uses this page. This protects the data of the
	 * page.
	 */
	vm_page_busy(page);

	if(vm_page_is_dirty(page)) {
		return object->pager->pageout(object, page);
	} else {
		return 0;
	}
}

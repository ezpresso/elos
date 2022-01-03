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
#include <vm/swap.h>
#include <vm/page.h>

/* 32 bits for 4kb blocks -> UINT32_MAX * PAGE_SIZE == enough
 * => don't have to use 64-bit block index
 */
typedef uint32_t vm_swapblk_t;

typedef struct vm_swappg {
	vm_pghash_node_t node;
	vm_swapblk_t blk;
} vm_swappg_t;

typedef struct vm_swapdev {
	blk_provider_t *dev;

	rb_tree_t spaces;

	/* freelist[0] -> size 1
	 * freelist[1] -> size 2
	 * freelist[2] -> size 4
	 * freelist[3] -> size 8
	 * are bigger sizes even needed?
	 */
	list_t freelist[VM_SWAP_FREELIST];

	/*
	 * Structure for managing swap blocks
	 */
	...
} vm_swapdev_t;

/*
 * PROBLEM this structure is too large
 */
typedef struct vm_swapspace {
	rb_node_t node;
	list_node_t free_node;
	vm_swapblk_t blk;
	vm_swapblk_t size;
	vm_swapblk_t free_size;
} vm_swapspace_t;

static vm_pager_t vm_swap_pager = {
	.flags = VM_PAGER_PGHASH,
	.pagein = vm_swap_pagein,
	.pageout = vm_swap_pageout,
};

static int vm_swap_pagein(vm_object_t *object, vm_pghash_node *node,
	vm_page_t **page)
{
	vm_swappg_t *swap = VM_SWAPPG(node);

}

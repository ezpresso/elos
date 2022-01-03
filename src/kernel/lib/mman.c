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
#include <lib/mman.h>
#include <config.h>

#if CONFIGURED(INVARIANTS)
void mman_check(mman_t *mman) {
	mman_node_t *node, *prev = NULL;

	rb_foreach(node, &node->node, &mman->nodes) {
		if(prev && prev->addr + prev->size + prev->free != node->addr) {
			mman_debug(mman);
			kpanic("[mman] error: 0x%llx + 0x%llx + 0x%llx "
				"!= 0x%llx", prev->addr, prev->size,
				prev->free, node->addr);
		}

		prev = node;
	}
}
#else
void mman_check(mman_t *mman) {
	(void) mman;
}
#endif

static void mman_node_free_add(mman_t *mman, mman_node_t *node) {
	mman_node_t *cur;

	if(node->free) {
		rb_insert(&mman->free, cur, &node->freenode, {
			if(node->free < cur->free) {
				goto left;
			} else {
				goto right;
			}
		});
	}
}

static void mman_node_free_rem(mman_t *mman, mman_node_t *node) {
	if(node->free) {
		rb_remove(&mman->free, &node->freenode);
	} else {
		/*
		 * The node should not be in the free tree,
		 * if map->free is zero.
		 */
		rb_node_assert_empty(&node->freenode);
	}
}

static void mman_node_add(mman_t *mman, mman_node_t *node) {
	mman_node_t *cur;

	rb_insert(&mman->nodes, cur, &node->node, {
		if(node->addr < cur->addr) {
			goto left;
		} else {
			goto right;
		}
	});
}

static void mman_node_set_free(mman_t *mman, mman_node_t *node, uint64_t free) {
	if(node->free == free) {
		return;
	}

	mman_node_free_rem(mman, node);
	node->free = free;
	mman_node_free_add(mman, node);
}

void mman_init(mman_t *mman, uint64_t start, uint64_t size) {
	rb_tree_init(&mman->nodes);
	rb_tree_init(&mman->free);
	mman_node_init(&mman->node);

	mman->node.addr = start;
	mman->node.size = 0;
	mman->node.free = size;
	mman_node_free_add(mman, &mman->node);
	mman_node_add(mman, &mman->node);
	mman_check(mman);
}

void mman_destroy(mman_t *mman) {
	rb_remove(&mman->nodes, &mman->node.node);
	mman_node_free_rem(mman, &mman->node);
	rb_tree_destroy(&mman->nodes);
	rb_tree_destroy(&mman->free);
	mman_node_destroy(&mman->node);
}

mman_node_t *mman_lookup(mman_t *mman, uint64_t addr) {
	mman_node_t *node;

	rb_search(&mman->nodes, node, {
		if(addr < node->addr) {
			goto left;
		} else if(node->size && addr < mman_node_end(node)) {
			return node;
		} else {
			goto right;
		}
	});

	return NULL;
}

mman_node_t *mman_first_node(mman_t *mman, uint64_t addr, uint64_t size) {
	mman_node_t *node = NULL, *cur;

	rb_search(&mman->nodes, cur, {
		if(mman_node_end(cur) > addr) {
			node = cur;

			if(cur->addr <= addr) {
				break;
			} else {
				goto left;
			}
		} else {
			goto right;
		}
	});

	if(!node || node->addr > addr - 1 + size) {
		return NULL;
	} else {
		return node;
	}
}

void mman_insert(mman_t *mman, uint64_t addr, uint64_t size,
	mman_node_t *node)
{
	mman_node_t *cur, *out = NULL;
	vm_vaddr_t end, free_end;

	/*
	 * Search the node which contains the region addr/size in its
	 * free region.
	 */
	rb_search(&mman->nodes, cur, {
		end = mman_node_end(cur);

		if(end < addr && addr < end + cur->free) {
			out = cur;
			break;
		} else if(cur->addr > addr) {
			goto left;
		} else {
			goto right;
		}
	});

	assert(out);
	free_end = end + out->free;

	node->addr = addr;
	node->size = size;
	node->free = free_end - mman_node_end(node);
	mman_node_free_add(mman, node);
	mman_node_add(mman, node);
	mman_node_set_free(mman, out, addr - end - 1);
	mman_check(mman);
}

int mman_alloc(mman_t *mman, uint64_t size, uint64_t align, mman_node_t *node) {
	mman_node_t *cur, *free = NULL;
	uint64_t start, alignsz;

	/*
	 * Search the smallest free-space which can fit the new node.
	 */
	rb_search(&mman->free, cur, {
		if(size > cur->free) {
			goto right;
		} else {
			start = cur->addr + cur->size;
			alignsz = ALIGN(start, align) - start;
			if(alignsz == 0 || cur->free >= size + alignsz) {
				free = cur;

				if(cur->free == size) {
					break;
				}
			}

			goto left;
		}
	});

	if(!free) {
		return -ENOMEM;
	}

	start = free->addr + free->size;
	node->addr = ALIGN(start, align);
	node->size = size;

	alignsz = node->addr - start;
	node->free = free->free - size - alignsz;

	mman_node_free_add(mman, node);
	mman_node_add(mman, node);
	mman_node_set_free(mman, free, alignsz);
	mman_check(mman);

	return 0;
}

void mman_free(mman_t *mman, mman_node_t *node) {
	mman_node_t *prev;

	prev = rb_prev(&node->node);
	mman_node_set_free(mman, prev, prev->free + node->free + node->size);
	mman_node_free_rem(mman, node);
	rb_remove(&mman->nodes, &node->node);
	mman_check(mman);
}

void mman_node_free_tail(mman_t *mman, mman_node_t *node, uint64_t size) {
	assert(size < node->size);

	node->size -= size;
	mman_node_set_free(mman, node, node->free + size);
	mman_check(mman);
}

void mman_node_free_head(mman_t *mman, mman_node_t *node, uint64_t size) {
	mman_node_t *prev;

	assert(size < node->size);

	prev = mman_node_prev(node);
	assert(prev);
	mman_node_set_free(mman, prev, prev->free + size);

	node->addr += size;
	node->size -= size;
	rb_remove(&mman->nodes, &node->node);
	mman_node_add(mman, node);

	mman_check(mman);
}

void mman_debug(mman_t *mman) {
	mman_node_t *node;

	rb_foreach(node, &node->node, &mman->nodes) {
		if(node != &mman->node) {
			kprintf("alloc: 0x%llx - 0x%llx\n",
				mman_node_addr(node), mman_node_end(node));
		}

		if(node->free) {
			kprintf("free:  0x%llx - 0x%llx\n",
				mman_node_end(node) + 1,
				mman_node_end(node) + node->free);
		}
	}
}

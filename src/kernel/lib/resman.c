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
#include <lib/resman.h>

static inline void resman_init(resman_t *resman) {
	list_node_init(resman, &resman->node);
	list_init(&resman->children);
}

static inline void resman_destroy(resman_t *resman) {
	list_destroy(&resman->children);
	list_node_destroy(&resman->node);
	resman->parent = NULL;
}

static inline void resman_set_range(resman_t *resman, resman_addr_t addr,
	resman_size_t size)
{
	resman->size = size;
	resman->addr = addr;
}

void resman_init_root(resman_t *resman, resman_addr_t addr, resman_addr_t end) {
	resman_init(resman);
	resman_set_range(resman, addr, end - addr + 1);
	resman->parent = NULL;
}

void resman_destroy_root(resman_t *resman) {
	resman_destroy(resman);
}

int resman_alloc_range(resman_t *parent, resman_t *result, resman_addr_t start,
	resman_addr_t end, resman_size_t size, resman_size_t alignment)
{
	/*
	 * The aligned address of the previous node in the foreach loop.
	 */
	resman_addr_t prev_end;
	resman_t *cur;

	assert(size != 0);
	assert(alignment > 0);

	resman_init(result);

	end = min(resman_get_end(parent), end) + 1;
	start = max(parent->addr, start);

	if(start >= end || end - start < size) {
		goto nospc;
	}

	prev_end = ALIGN(start, alignment);

	/* look through the list and search for holes */
	foreach(cur, &parent->children) {
		if(prev_end >= end) {
			/* if _end_ is reached, the allocation failed */
			goto nospc;
		}

		if(cur->addr < prev_end) {
			continue;
		}

		if(cur->addr - prev_end >= size) {
			list_insert_before(&parent->children, &cur->node,
				&result->node);
			resman_set_range(result, prev_end, size);
			goto success;
		}

		prev_end = ALIGN(resman_get_end(cur)+1, alignment);
	}

	/*
	 * Maybe there is space at the end.
	 */
	if(prev_end < end && end - prev_end >= size) {
		resman_set_range(result, prev_end, size);
		list_append(&parent->children, &result->node);
		goto success;
	}

nospc:
	resman_destroy(result);
	return -ENOSPC;

success:
	result->parent = parent;
	return 0;
}

int resman_alloc(resman_t *parent, resman_t *result, resman_size_t size,
	resman_size_t align)
{
	return resman_alloc_range(parent, result, parent->addr,
			resman_get_end(parent), size, align);
}

int resman_reserve(resman_t *parent, resman_t *rsvd, resman_addr_t start,
	resman_addr_t end)
{
	return resman_alloc_range(parent, rsvd, start, end, end - start + 1, 1);
}

int resman_shrink(resman_t *rm, resman_size_t size) {
	resman_t *last;

	if(rm->size < size) {
		kpanic("[resman] shrink: size too big: 0x%llx", size);
	}

	if(rm->size == size) {
		return 0;
	}

	last = list_last(&rm->children);
	if(last && resman_get_end(last) > rm->addr + size - 1) {
		return -ENOTSUP;
	}

	rm->size = size;

	return 0;
}

void resman_free(resman_t *resman) {
	list_remove(&resman->parent->children, &resman->node);
	resman_destroy(resman);
}

resman_t *resman_lookup(resman_t *parent, resman_addr_t addr) {
	resman_t *cur;

	foreach(cur, &parent->children) {
		if(cur->addr <= addr && resman_get_end(cur) >= addr) {
			return cur;
		}
	}

	return NULL;
}

void resman_print(resman_t *resman) {
	resman_t *cur;

	foreach(cur, &resman->children) {
		kprintf("0x%llx - 0x%llx\n", resman_get_addr(cur),
			resman_get_end(cur));
	}
}

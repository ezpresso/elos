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
#include <vm/malloc.h>
#include <lib/cbuf.h>
#include <lib/string.h>

int cbuf_alloc(cbuf_t *cbuf, size_t size, vm_flags_t flags) {
	cbuf->first = kmalloc(size, flags);
	if(cbuf->first == NULL) {
		return -ENOMEM;
	}

	cbuf->last = cbuf->first + size;
	cbuf->rptr = cbuf->first;
	cbuf->wptr = cbuf->first;
	cbuf->data = 0;

	return 0;
}

void cbuf_free(cbuf_t *cbuf) {
	kfree(cbuf->first);
}

static size_t cbuf_iter_max(cbuf_t *cbuf, void *ptr, size_t size) {
	return min((size_t)(cbuf->last - ptr), size);
}

size_t cbuf_read(cbuf_t *cbuf, size_t size, void *buf) {
	size_t total, cur;

	total = size = min(cbuf->data, size);
	cbuf->data -= size;

	while(size) {
		cur = cbuf_iter_max(cbuf, cbuf->rptr, size);
		memcpy(buf, cbuf->rptr, cur);

		size -= cur;
		buf += cur;
		cbuf->rptr += cur;
		if(cbuf->rptr == cbuf->last) {
			cbuf->rptr = cbuf->first;
		}
	}

	return total;
}

size_t cbuf_write(cbuf_t *cbuf, size_t size, void *buf) {
	size_t total, cur;

	total = size = min(cbuf_size(cbuf) - cbuf->data, size);
	cbuf->data += size;

	while(size) {
		cur = cbuf_iter_max(cbuf, cbuf->wptr, size);
		memcpy(cbuf->wptr, buf, cur);

		size -= cur;
		buf += cur;
		cbuf->wptr += cur;
		if(cbuf->wptr == cbuf->last) {
			cbuf->wptr = cbuf->first;
		}
	}

	return total;
}

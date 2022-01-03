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
#include <lib/bitset.h>
#include <vm/malloc.h>

void bset_init(bset_t *set, uint8_t *data, size_t size) {
	set->bitset = data;
	set->size = size;
}

int bset_alloc(bset_t *set, size_t size) {
	void *buf;

	/*
	 * TODO allow a way to set VM_WAIT flag.
	 */
	buf = kmalloc(ALIGN(size, 8) >> 3, VM_ZERO);
	if(!buf) {
		return -ENOMEM;
	}

	bset_init(set, buf, size);
	return 0;
}

void bset_free(bset_t *set) {
	kfree(set->bitset);
	set->size = 0;
}

void bset_set(bset_t *set, size_t bit) {
	assert(bit < set->size);
	bset(&set->bitset[bit >> 3], bit & 0x7);
}

void bset_clr(bset_t *set, size_t bit) {
	assert(bit < set->size);
	bclr(&set->bitset[bit >> 3], bit & 0x7);
}

bool bset_test(bset_t *set, size_t bit) {
	return !!((set->bitset[bit >> 3]) & (1 << (bit & 0x7)));
}

int bset_ffs(bset_t *set) {
	size_t i;
	int bit;

	for(i = 0; i < set->size; i += 8) {
		bit = ffs(set->bitset[i >> 3]);
		if(bit > 0) {
			bit += i;

			if((size_t)bit > set->size) {
				return -ENOSPC;
			}

			return bit;
		}
	}

	return 0;
}

int bset_alloc_bit(bset_t *set) {
	size_t i;
	int bit;

	for(i = 0; i < set->size; i += 8) {
		bit = ffs((~set->bitset[i >> 3]) & 0xFF);
		if(bit > 0) {
			bit--;
			bit += i;

			if((size_t)bit >= set->size) {
				goto err;
			}

			bset_set(set, bit);
			return bit;
		}
	}

err:
	return -ENOSPC;
}

void bset_free_bit(bset_t *set, size_t bit) {
	bset_clr(set, bit);
}

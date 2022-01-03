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
#include <lib/hashtab.h>
#include <vm/malloc.h>
#include <sys/pow2.h>

int hashtab_alloc(hashtab_t *tab, size_t nentries, vm_flags_t flags) {
	VM_FLAGS_CHECK(flags, VM_WAIT);

	tab->nentries = next_pow2(nentries);
	tab->entries = kmalloc(sizeof(list_t) * tab->nentries, flags);
	if(!tab->entries) {
		return -ENOMEM;
	}

	for(size_t i = 0; i < tab->nentries; i++) {
		list_init(&tab->entries[i]);
	}

	return 0;
}

void hashtab_free(hashtab_t *tab) {
	for(size_t i = 0; i < tab->nentries; i++) {
		list_destroy(&tab->entries[i]);
	}

	kfree(tab->entries);
}

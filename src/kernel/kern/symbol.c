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
#include <kern/init.h>
#include <kern/symbol.h>
#include <lib/string.h>

/**
 * @brief The size of the symbol hashtable.
 */
#define SYMBOL_HT 4096

static list_t symbol_ht[SYMBOL_HT];

/**
 * @brief Calculate the hash of an exported kernel symbol.
 */
static inline size_t symbol_hash(const char *string, const size_t len) {
	size_t i, hash = 0;

	for(i = 0; i < len; ++i) {
		hash = 65599 * hash + string[i];
	}

	return (hash ^ (hash >> 16)) & (SYMBOL_HT - 1);
}

uintptr_t symbol_get(const char *name) {
	size_t hash, len = strlen(name);
	kern_sym_t *sym;

	hash = symbol_hash(name, len);
	foreach(sym, &symbol_ht[hash]) {
		if(sym->len == len && !strcmp(sym->name, name)) {
			return sym->addr;
		}
	}

	return SYMBOL_NONE;
}

#if 0
void symbol_lookup(uintptr_t addr, const char **name, size_t *off) {

}
#endif

void symbol_add_symtab(kern_sym_t *sym, size_t size) {
	for(size_t i = 0; i < size; i++) {
		size_t hash;

		list_node_init(&sym[i], &sym[i].node);

		/*
		 * Add the symbol to the hashtable.
		 */
		hash = symbol_hash(sym[i].name, sym[i].len);
		list_append(&symbol_ht[hash], &sym[i].node);
	}
}

void __init init_symbol(void) {
	for(size_t i = 0; i < SYMBOL_HT; i++) {
		list_init(&symbol_ht[i]);
	}

	/*
	 * Add the exported symbols from the kernel binary.
	 */
	symbol_add_symtab(section_first(SYMBOL_SECTION),
		secton_nelem(SYMBOL_SECTION));
}

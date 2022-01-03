#ifndef KERN_SYMBOL_H
#define KERN_SYMBOL_H

#include <kern/section.h>
#include <lib/list.h>

#define SYMBOL_SECTION	section(kernsym, kern_sym_t)
#define SYMBOL_NONE 	0

/* TODO only if modular */
#define export(sym)							\
	static section_entry(SYMBOL_SECTION) kern_sym_t sym ## _sym = {	\
		.name = # sym,						\
		.len = sizeof(#sym) - 1,				\
		.addr = (uintptr_t)&sym,				\
	}

typedef struct kern_sym {
	list_node_t node;
	const char *name;
	const size_t len;
	uintptr_t addr;
} kern_sym_t;

/**
 * @brief Get the address of a kernel symbol.
 *
 * Get the address of a kernel symbol exported to modules by using
 * export().
 *
 * @param name The name of the symbol as a string.
 * @return  SYMBOL_NONE if there is no such symbol.
 * 			The address of the symbol.
 */
uintptr_t symbol_address(const char *name);

void symbol_lookup(uintptr_t addr, const char **name, const char **loc,
		size_t *off);

void init_symbol(void);

#endif

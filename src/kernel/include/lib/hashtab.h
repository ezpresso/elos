#ifndef LIB_HASHTABLE_H
#define LIB_HASHTABLE_H

#include <lib/list.h>
#include <vm/flags.h>

#define HASHTAB_IDX(ht, hash)	((hash) & ((ht)->nentries - 1))
#define HASHTAB_LIST(ht, hash) 	(&(ht)->entries[HASHTAB_IDX(ht, hash)])

#define hashtab_search(entry, hash, tab) \
	foreach(entry, HASHTAB_LIST(tab, hash))

typedef struct hashtab {
	size_t nentries; /* Number of entries */
	list_t *entries;
} hashtab_t;

/**
 * @brief Initialize an hashtable.
 */
int hashtab_alloc(hashtab_t *tab, size_t nentries, vm_flags_t flags);

/**
 * @brief Initialize an hashtable.
 */
int hashtab_alloc_locked(hashtab_t *tab, size_t nentries, vm_flags_t flags);

/**
 * Free a hashtable.
 */
void hashtab_free(hashtab_t *tab);

/**
 * @brief Add an entry to an hashtable.
 */
static inline void hashtab_set(hashtab_t *tab, size_t hash,
	list_node_t *node)
{
	list_append(HASHTAB_LIST(tab, hash), node);
}

/**
 * @brief Remove an entry from an hashtable.
 */
static inline void hashtab_remove(hashtab_t *tab, size_t hash,
	list_node_t *node)
{
	list_remove(HASHTAB_LIST(tab, hash), node);
}

static inline void hashtab_rehash(hashtab_t *tab, size_t ohash, size_t nhash,
	list_node_t *node)
{
	if(ohash != nhash) {
		hashtab_remove(tab, ohash, node);
		hashtab_set(tab, nhash, node);
	}
}

/**
 * @brief A string hashing function
 */
static inline size_t hash_str(const char *str) {
	size_t hashval = 0;

	for(; *str != '\0'; str++) {
		hashval = *str + (hashval << 5) - hashval;
	}

	return hashval;
}

#endif
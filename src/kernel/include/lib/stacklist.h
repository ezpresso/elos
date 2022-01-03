#ifndef LIB_STACKLIST_H
#define LIB_STACKLIST_H

#include <kern/atomic.h>

typedef struct stacklist_item {
	struct stacklist_item *next;
	void *value;
} stacklist_item_t;

typedef struct stacklist {
	stacklist_item_t *first;
} stacklist_t;

#define STACKLIST_INIT (stacklist_t) { .first = NULL }

static inline void stacklist_init(stacklist_t *list) {
	*list = STACKLIST_INIT;
}

static inline void stacklist_item_init(void *val, stacklist_item_t *item) {
	item->next = NULL;
	item->value = val;
}

static inline void stacklist_push_atomic(stacklist_t *list,
	stacklist_item_t *item)
{
	do {
		item->next = atomic_load(&list->first);
	} while(!atomic_cmpxchg(&list->first, item->next, item));
}

static inline void *stacklist_pop_atomic(stacklist_t *list) {
	stacklist_item_t *item;
	
	do {
		item = atomic_load(&list->first);
		if(!item) {
			return NULL;
		}
	} while(!atomic_cmpxchg(&list->first, item, item->next));

	return item->value;
}

#endif
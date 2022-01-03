#ifndef LIB_LIST_LOCKED_H
#define LIB_LIST_LOCKED_H

#include <kern/sync.h>

typedef struct locklist {
	list_t list;
	sync_t lock;
} locklist_t;

#define DEFINE_LOCKLIST(name, lk) \
	locklist_t name = { \
		.list = __LIST_FIELDS_INIT(&name.list), \
		.lock = __SYNC_INIT(lk), \
	}

static inline void __list_locked_cleanup(list_node_t **x) {
	sync_t *lock = (sync_t *)*x;
	sync_release(lock);
}

#define foreach_locked(item, list, lock)			\
	for(list_node_t *__cleanup(__list_locked_cleanup)	\
		__unused __lock = ({ 				\
			sync_acquire(lock);			\
			(list_node_t *)lock;			\
		}), *__next = __foreach_init(item, list);	\
		(item) != NULL;					\
		__foreach_next(list, item, __next))

#define ll_foreach(item, llist) \
	foreach_locked(item, &(llist)->list, &(llist)->lock)

#define locklist_next(l, node)	list_next_locked(&(l)->list, node, &(l)->lock)
#define locklist_prev(l, node)	list_prev_locked(&(l)->list, node, &(l)->lock)
#define locklist_first(l)	list_first_locked(&(l)->list, &(l)->lock)
#define locklist_last(l)	list_last_locked(&(l)->list, &(l)->lock)
#define locklist_length(l)	list_length(&(l)->list, &(l)->lock)
#define locklist_add(l, node)	list_add_locked(&(l)->list, node, &(l)->lock)
#define locklist_append(l, node) list_append_locked(&(l)->list, node, &(l)->lock)
#define locklist_remove(l, node) list_remove_locked(&(l)->list, node, &(l)->lock)
#define locklist_pop_front(l)	list_pop_front_locked(&(l)->list, &(l)->lock)

static inline void locklist_init(locklist_t *list, int lock) {
	list_init(&list->list);
	sync_init(&list->lock, lock);
}

static inline void locklist_destroy(locklist_t *list) {
	sync_destroy(&list->lock);
	list_destroy(&list->list);
}

static inline void *list_next_locked(list_t *list, list_node_t *node,
		sync_t *sync)
{
	sync_scope_acquire(sync);
	return list_next(list, node);
}

static inline void *list_prev_locked(list_t *list, list_node_t *node,
		sync_t *sync)
{
	sync_scope_acquire(sync);
	return list_prev(list, node);
}

static inline void *list_first_locked(list_t *list, sync_t *sync) {
	sync_scope_acquire(sync);
	return list_first(list);
}

static inline void *list_last_locked(list_t *list, sync_t *sync) {
	sync_scope_acquire(sync);
	return list_last(list);
}

static inline size_t list_length_locked(list_t *list, sync_t *sync) {
	sync_scope_acquire(sync);
	return list_length(list);
}

static inline void list_add_locked(list_t *list, list_node_t *new,
		sync_t *sync)
{
	sync_scope_acquire(sync);
	list_add(list, new);
}

static inline void list_append_locked(list_t *list, list_node_t *new,
		sync_t *sync)
{
	sync_scope_acquire(sync);
	list_append(list, new);
}

#define list_remove_locked(l, n, s)  ({	\
		sync_scope_acquire(s);	\
		list_remove(l, n);	\
	})

static inline void *list_pop_front_locked(list_t *list, sync_t *sync) {
	sync_scope_acquire(sync);
	return list_pop_front(list);
}

static inline void list_insert_before_locked(list_t *list, list_node_t *next,
		list_node_t *new, sync_t *sync)
{
	sync_scope_acquire(sync);
	list_insert_before(list, next, new);
}

static inline void list_insert_after_locked(list_t *list, list_node_t *prev,
		list_node_t *new, sync_t *sync)
{
	sync_scope_acquire(sync);
	list_insert_after(list, prev, new);
}

#endif

#ifndef __ELOS_LIST_H__
#define __ELOS_LIST_H__

#ifndef __KERNEL__
#define __LIST
#define __LIST_INIT
#define __list_init(list)
#define __list_check(list)
#define __list_destroy(list)
#define __LIST_NODE
#define __list_node_init(node)
#define __list_node_check(node)
#define __list_node_destroy(node)
#define __list_assert_owner(list, node)
#define __list_insert(list, node)
#define __list_remove(list, node)
#endif

#define __list_node_check_not_inserted(node) \
	__list_node_check(node); \
	assert((node)->prev == NULL && (node)->next == NULL && \
		"[list] node still in list");

#define __list_check_insert(list, node) ({ \
	__list_check(list); \
	__list_node_check_not_inserted(node); \
})

#define __list_check_inserted(list, node) ({ \
	__list_check(list); \
	__list_node_check(node); \
	__list_assert_owner(list, node); \
})

typedef struct list_node {
	struct list_node *next, *prev;
	union {
		void *value;
		size_t length;
	};
	__LIST_NODE
} list_node_t;

typedef struct list {
	list_node_t node;
	__LIST
} list_t;

#define __LIST_FIELDS_INIT(var) { \
	.node = { \
		&(var)->node, \
		&(var)->node, \
		{ NULL }, \
		__LIST_NODE_INIT(var) \
	}, \
	__LIST_INIT \
}

#define DEFINE_LIST(var) list_t var = __LIST_FIELDS_INIT(&var)

static inline void list_init(list_t *list) {
	list->node.next = &list->node;
	list->node.prev = &list->node;
	list->node.length = 0;
	__list_node_init(&list->node);
	__list_init(list);
}

static inline void list_node_init(void *value, list_node_t *node) {
	node->next = NULL;
	node->prev = NULL;
	node->value = value;
	__list_node_init(node);
}

static inline bool list_is_empty(list_t *list) {
	__list_check(list);
	return list->node.next == &list->node;
}

static inline void list_destroy(list_t *list) {
	assert(list_is_empty(list) && "[list] destroy: list not empty");
	__list_destroy(list);
	__list_node_destroy(&list->node);
}

static inline void list_node_destroy(list_node_t *node) {
	__list_node_check_not_inserted(node);
	__list_node_destroy(node);
}

static inline void *list_node_val(list_node_t *node) {
	return node ? node->value : NULL;
}

static inline list_node_t *list_next_node(list_t *list, list_node_t *node) {
	__list_check_inserted(list, node);
	return node->next == &list->node ? NULL : node->next;
}

static inline list_node_t *list_prev_node(list_t *list, list_node_t *node) {
	__list_check_inserted(list, node);
	return node->prev == &list->node ? NULL : node->prev;
}

static inline void *list_next(list_t *list, list_node_t *node) {
	return list_node_val(list_next_node(list, node));
}

static inline void *list_prev(list_t *list, list_node_t *node) {
	return list_node_val(list_prev_node(list, node));
}

static inline list_node_t *list_first_node(list_t *list) {
	return list_next_node(list, &list->node);
}

static inline list_node_t *list_last_node(list_t *list) {
	return list_prev_node(list, &list->node);
}

static inline void *list_first(list_t *list) {
	return list_next(list, &list->node);
}

static inline void *list_last(list_t *list) {
	return list_prev(list, &list->node);
}

static inline size_t list_length(list_t *list) {
	__list_check(list);
	return list->node.length;
}

static inline void list_insert_after(list_t *list, list_node_t *node,
	list_node_t *new)
{
	__list_check_inserted(list, node);
	__list_node_check_not_inserted(new);
	__list_insert(list, new);
	new->next = node->next;
	new->prev = node;
	new->next->prev = new;
	new->prev->next = new;
	list->node.length++;
}

static inline void list_insert_before(list_t *list, list_node_t *node,
	list_node_t *new)
{
	__list_check_inserted(list, node);
	__list_node_check_not_inserted(new);
	__list_insert(list, new);
	new->next = node;
	new->prev = node->prev;
	new->next->prev = new;
	new->prev->next = new;
	list->node.length++;
}

static inline void list_append(list_t *list, list_node_t *node) {
	return list_insert_before(list, &list->node, node);
}

static inline void list_add(list_t *list, list_node_t *node) {
	return list_insert_after(list, &list->node, node);
}

static inline bool list_remove(list_t *list, list_node_t *node) {
	__list_check_inserted(list, node);
	__list_remove(list, node);
	assert(list->node.length);
	node->prev->next = node->next;
	node->next->prev = node->prev;
	node->prev = NULL;
	node->next = NULL;
	return --list->node.length == 0;
}

static inline list_node_t *list_node_pop_front(list_t *list) {
	list_node_t *node = list_first_node(list);
	if(node) {
		list_remove(list, node);
	}
	return node;
}

static inline void *list_pop_front(list_t *list) {
	return list_node_val(list_node_pop_front(list));
}

static inline void *list_get(list_t *list, size_t idx) {
	list_node_t *node;
	size_t i;

	__list_check(list);
	if(idx >= list_length(list)) {
		return NULL;
	}

	node = list_first_node(list);
	for(i = 0; i < idx; i++) {
		assert(node);
		node = node->next;
	}

	return node->value;
}

static inline size_t *list_length_ptr(list_t *list) {
	return &list->node.length;
}

static inline bool list_node_end_p(list_t *list, list_node_t *node) {
	return node == &list->node;
}

#define __foreach_init(item, list) ({ \
	(item) = list_first(list); \
	(item) ? list_next_node(list, list_first_node(list)) : NULL; \
})

#define __foreach_next(list, item, __next) \
	(item) = list_node_val(__next), \
	(__next) = (__next) ? list_next_node(list, __next) : NULL

#define foreach(item, list) \
	for(list_node_t *__next = __foreach_init(item, list); \
		(item) != NULL;	\
		__foreach_next(list, item, __next))

#endif

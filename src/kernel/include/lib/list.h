#ifndef LIB_LIST_H
#define LIB_LIST_H

#include <lib/development.h>

/*
 * The kernel uses the elos standard list implementation (sys/list.h),
 * however the kernel does some more error checking.
 */
#define __LIST \
	MAGIC(magic);

#define __LIST_NODE \
	DEVEL_VAR(void *, owner); \
	MAGIC(magic);

#define __LIST_INIT \
	MAGIC_INIT(magic, LIST_MAGIC)

#define __LIST_NODE_INIT(list) \
	DEVEL_INIT(owner, list), \
	MAGIC_INIT(magic, LIST_MAGIC),

#define __list_init(list) \
	magic_init(&(list)->magic, LIST_MAGIC); \
	__list_insert(list, &(list)->node)

#define __list_check(list) \
	magic_check(&(list)->magic, LIST_MAGIC);

#define __list_destroy(list) \
	magic_destroy(&(list)->magic);

#define __list_node_init(node) \
	magic_init(&(node)->magic, LIST_MAGIC); \
	DEVEL_SET((node)->owner, NULL);	\
	devel_prot(&(node)->value); \

#define __list_node_destroy(node) \
	magic_destroy(&(node)->magic); \
	devel_rmprot(&(node)->value); \

#define __list_node_check(node) \
	magic_check(&(node)->magic, LIST_MAGIC);

#define __list_assert_owner(list, node) ({ \
	devel_assert(&((node)->owner), list, "[list] node->owner (0x%p) != " \
		"list (0x%p)", (node)->owner, list); \
})

#define __list_insert(list, node) ({ \
	devel_assert(&(node)->owner, NULL, "[list] node->owner is not NULL"); \
	DEVEL_SET((node)->owner, list);	\
})

#define __list_remove(list, node) DEVEL_SET((node)->owner, NULL);

#include <elos/list.h>

#endif

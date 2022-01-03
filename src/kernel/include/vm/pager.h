#ifndef VM_PAGER_H
#define VM_PAGER_H

struct vm_object;
struct vm_pghash_node;
struct vm_page;

typedef int (vm_pager_pagein_t) (struct vm_object *,
	struct vm_pghash_node *node, struct vm_page *);
typedef int (vm_pager_pageout_t) (struct vm_object *, struct vm_page *);

/*
What was this callback for?
typedef void (vm_pager_discard_t) (struct vm_object *,
	struct vm_pghash_node *node); */

/**
 * If this flag is set, the pagein code doesn't call the pagein callback
 * of the pager in case no pghash_node is in the pghash.
 * TODO consider adding an hashpage callback
 */
#define VM_PAGER_PGHASH	(1 << 0)

typedef struct vm_pager {
	int flags;
	vm_pager_pagein_t	*pagein;
	vm_pager_pageout_t	*pageout;
} vm_pager_t;

int vm_pager_pagein(struct vm_object *object, vm_objoff_t offset,
	struct vm_pghash_node *node, struct vm_page **pagep);

int vm_pager_pageout(struct vm_object *object, struct vm_page *page);

#endif

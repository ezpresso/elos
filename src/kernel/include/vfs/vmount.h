#ifndef VFS_VMOUNT_H
#define VFS_VMOUNT_H

#include <kern/atomic.h>
#include <kern/rwlock.h>
#include <lib/list.h>

#define VMOUNT_RO (1 << 0)

typedef struct vmount {
	/*
	 * Mounting a filesystem does not happen very often,
	 * but lookups are very common -> use a rwlock.
	 * --- maybe remove this lock and add a global mountlock
	 */
	rwlock_t lock;
	ref_t ref;

	list_node_t node;
	list_t children;

	int flags;

	/*
	 * The fields below are constant.
	 */
	struct vmount *parent;
	struct filesys *filesys;
	ino_t root;
	ino_t mountpoint;
} vmount_t;

static inline vmount_t *vmount_ref(vmount_t *mount) {
	ref_inc(&mount->ref);
	return mount;
}

static inline void vmount_unref(vmount_t *mount) {
	(void) mount;
	/* kprintf("TODO vmount unref\n"); */
#if notyet
	if(ref_dec(&mount->ref) && (mount->flags & VMOUNT_UNMOUNT)) {
		if(parent) {
			rwlocked(&mount->parent->lock) {
				if(ref_get(&mount->ref) != 0) {
					return;
				}
				list_remove(...);
			}
		}

		vmount_unref(mount->parent);
		filesys_unref(mount->filesys);
		free(mount);
	}
#endif
}

static inline bool vmount_writeable(vmount_t *mount) {
	return !(mount->flags & VMOUNT_RO);
}

static inline void vmount_remove(vmount_t *parent, vmount_t *mount) {
	wrlocked(&parent->lock) {
		list_remove(&parent->children, &mount->node);
	}

	mount->parent = NULL;
	vmount_unref(parent);
}

vmount_t *vmount_alloc(ino_t mountpoint, int flags);
void vmount_free(vmount_t *mount);

void vmount_insert(vmount_t *parent, vmount_t *mount, struct filesys *fs,
	ino_t root);

vmount_t *vmount_lookup(vmount_t *parent, ino_t ino);
bool vmount_mountpoint_p(vmount_t *parent, ino_t ino);

#endif
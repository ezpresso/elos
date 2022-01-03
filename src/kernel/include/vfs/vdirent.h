#ifndef VFS_VDIRENT_H
#define VFS_VDIRENT_H

#include <lib/list.h>
#include <vm/flags.h>

struct filesys;

#define VDNAME_INLINE	32
#define VD_PERM		(1 << 0)

/**
 * @brief A directory entry
 */
typedef struct vdirent {
	struct filesys *fs;
	list_node_t node; /* node for name cache */
	list_node_t lru_node;
	ino_t owner; /* might not be needed */
	ino_t ino;
	int flags;

	size_t namelen;
	union {
		char name[VDNAME_INLINE];
		char *bigname;
	};
} vdirent_t;

/**
 * @brief Get the name of a dirent.
 */
static inline const char *vdirent_name(vdirent_t *dirent) {
	if(dirent->namelen >= VDNAME_INLINE) {
		return dirent->bigname;
	} else {
		return dirent->name;
	}
}

/**
 * @brief Allocate a new dirent.
 */
vdirent_t *vdirent_alloc(struct filesys *fs, const char *name, size_t length,
		ino_t owner, ino_t ino, int flags, vm_flags_t allocflags);

/**
 * @brief Free a dirent.
 */
void vdirent_free(vdirent_t *dirent);

#endif
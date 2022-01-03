#ifndef VFS_VPATH_H
#define VFS_VPATH_H

#include <vfs/_vpath.h>
#include <vfs/vnode.h>
#include <vfs/vmount.h>

#define __VPATH_INIT { NULL, NULL }
#define VPATH_INIT (vpath_t){ NULL, NULL }

#define ASSERT_VPATH_NULL(p) assert(vpath_is_empty(p))

static inline void vpath_init(vpath_t *path) {
	*path = VPATH_INIT;
}

static inline bool vpath_is_empty(vpath_t *path) {
	if(path->node == NULL) {
		assert(path->mount == NULL);
		return true;
	} else {
		return false;
	}
}

static inline void vpath_clear(vpath_t *path) {
	if(!vpath_is_empty(path)) {
		vmount_unref(path->mount);
		vnode_unref(path->node);
		vpath_init(path);
	}
}

static inline void vpath_set(vpath_t *path, vmount_t *mount, vnode_t *node) {
	if(!vpath_is_empty(path)) {
		vpath_clear(path);
	}

	path->mount = vmount_ref(mount);
	path->node = vnode_ref(node);
}

static inline void vpath_cpy(vpath_t *dst, vpath_t *src) {
	assert(!vpath_is_empty(src));
	vpath_set(dst, src->mount, src->node);
}

#endif
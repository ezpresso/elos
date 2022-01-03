#ifndef VFS__VPATH_H
#define VFS__VPATH_H

struct vnode;
struct vmount;

typedef struct vpath {
	struct vnode *node;
	struct vmount *mount;
} vpath_t;

#endif
#ifndef VFS_NAMEI_H
#define VFS_NAMEI_H

#include <vfs/_vpath.h>

struct vpath;
struct vnode;

#define VNAMEI_NOFOLLOW	(1 << 0) /* -> O_NOFOLLOW */
#define VNAMEI_DIR	(1 << 1) /* search directory */
#define VNAMEI_ANY	(1 << 2) /* search directory or normal file */
#define VNAMEI_EXCL	(1 << 3) /* for VNAMEI_CREATE */
#define VNAMEI_EMPTY	(1 << 4) /* allow empty path */
#define VNAMEI_OPTIONAL	(1 << 5) /* used in conjunction with LOCKPARENT */
#define VNAMEI_LOCKPARENT (1 << 6) /* return the parent in a locked state */
#define VNAMEI_WANTPARENT (1 << 7) /* only lookup parent (returned unlocked) */
#define VNAMEI_EROFS	(1 << 8)

typedef enum vnamei_op {
	VNAMEI_LOOKUP,
	VNAMEI_CREATE,
	VNAMEI_UNLINK,
	VNAMEI_RENAME,
} vnamei_op_t;

typedef struct vnamei {
	vpath_t path;
	vpath_t parent;
	char *childname;
	size_t namelen;
	struct vdirent *dirent;
} vnamei_t;

int vnamei(vpath_t *at, const char *path, int flags, vnamei_op_t op,
	vnamei_t *namei);

void vnamei_done(vnamei_t *namei);
void vnamei_done_unlock(vnamei_t *namei);

int vlookup(vpath_t *at, const char *path, int flags, vpath_t *result);
int vlookup_node(vpath_t *at, const char *path, int flags, struct vnode **out);

#endif
#ifndef VFS_VFS_H
#define VFS_VFS_H

/* Lock order:
 * 1. vnode->lock
 * 2. filesys->lock
 * 3. vnode->object.lock
 * 4. vnode->statlock
 *
 * Why? Because devfs needs to lock the fs on file creation.
 * AND namei needs this
 */

struct vpath;
struct file;

extern struct vpath vfs_root;

/**
 * @brief Create a file / directory in thr filesystem.
 * @brief flags VNAMEI_EXCL
 */
int kern_create(struct vpath *at, const char *path, int flags, mode_t mode,
	struct vpath *result, ... /* dev_t dev / const char *symlink */);

int kern_mkdirat(struct vpath *at, const char *path, mode_t mode);

int kern_mknodat(struct vpath *at, const char *path, mode_t mode, dev_t dev);

int kern_symlinkat(const char *target, struct vpath *at, const char *linkpath);

int kern_unlinkat(struct vpath *at, const char *path, int flags);

int kern_renameat(struct vpath *oldat, const char *old, struct vpath *newat,
	const char *new);

int kern_openat(struct vpath *at, const char *path, int flags, mode_t mode,
	struct file **result);

int kern_open(const char *path, int flags, mode_t mode, struct file **result);

void kern_close(struct file *file);

int kern_mount(const char *source, const char *target, const char *type,
	unsigned long flags, const void *data);

int vfs_dirpath(struct vpath *dir, char *buf, size_t buflen);

void init_vfs(void);

#endif
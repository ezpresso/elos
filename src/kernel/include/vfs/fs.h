#ifndef VFS_FS_H
#define VFS_FS_H

#include <kern/atomic.h>
#include <kern/driver.h>
#include <kern/sync.h>
#include <lib/list.h>

struct filesys;
struct vnode;
struct statfs;

#define FS_DRIVER(name)			\
	/* static */ fs_driver_t name;	\
	driver(&name, DRIVER_FILESYS);	\
	static fs_driver_t name =

typedef struct fs_driver {
	DRIVER_COMMON;

#define FSDRV_SINGLETON	(1 << 0)
#define FSDRV_SOURCE	(1 << 1)
	int flags;

	int  (*mount)	(struct filesys *, ino_t *root);
	void (*unmount)	(struct filesys *);

	int (*vget) 	(struct filesys *, ino_t, struct vnode **);
	void (*vput)	(struct filesys *, struct vnode *);

	void (*statfs)	(struct filesys *, struct statfs *);

	/* used for singleton */
	struct filesys *fs; /* This does not count as a reference */
} fs_driver_t;

typedef struct filesys {
	fs_driver_t *driver;
	ref_t ref;
	ref_t sref; /* strong ref, TODO WHAT IS THIS? */
	sync_t lock;
	void *priv;
	ino_t root;

	struct blk_provider *dev;

	/*
	 * MS_RDONLY
	 */
	int flags;

	/* uint64_t vn_ino_cnt; //vnodes currently used
	 * uint64_t vc_ino_cnt; //vnodes in cache (with refcnt = 0)
 	 */
} filesys_t;

#if notyet
/* Atomically check if a strong reference is allowed (i.e. the
 * filesystem is currently not being unmounted) and increment
 * the strong reference count if necessary.
 */
bool filesys_ref_strong(filesys_t *fs);
void filesys_unref_strong(filesys_t *fs);
#endif

static inline const char *filesys_name(filesys_t *fs) {
	return fs->driver->name;
}

static inline void *filesys_get_priv(filesys_t *fs) {
	return fs->priv;
}

static inline void filesys_set_priv(filesys_t *fs, void *priv) {
	kassert(fs->priv == NULL, "[fs] multiple driver private pointers");
	fs->priv = priv;
}

static inline int filesys_vget(filesys_t *fs, ino_t ino,
	struct vnode **result)
{
	sync_scope_acquire(&fs->lock);
	/* TODO if(fs->unmounted) { return; } */
	return fs->driver->vget(fs, ino, result);
}

static inline void filesys_vput(filesys_t *fs, struct vnode *node) {
	sync_assert(&fs->lock);
	fs->driver->vput(fs, node);
}

static inline void filesys_stat(filesys_t *fs, struct statfs *statfs) {
	fs->driver->statfs(fs, statfs);
}

static inline struct blk_provider *filesys_dev(filesys_t *fs) {
	return fs->dev;
}

static inline filesys_t *filesys_ref(filesys_t *fs) {
	ref_inc(&fs->ref);
	return fs;
}

static inline void filesys_unref(filesys_t *fs) {
	if(ref_dec(&fs->ref)) {
#if notyet
		if(fs->flags & FS_UNMOUNT) {
			...
		}
#else
		kpanic("filesys: refcnt is zero");
#endif	
	}
}

int filesys_mount(const char *source, const char *fstype, int flags,
	const char *data, filesys_t **out);

#endif
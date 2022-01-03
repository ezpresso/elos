#ifndef VFS_VOP_H
#define VFS_VOP_H

#include <vm/flags.h>

#define VOP_PANIC (void *)vop_panic

typedef struct vnode_create_args {
	mode_t mode;
	uid_t uid;
	gid_t gid;

	union {
		dev_t dev;
	
		struct {
			const char *name;
			size_t len;
		} symlink;
	};
} vnode_create_args_t;

typedef int 	vop_open_t	(struct vnode *, int);
typedef void 	vop_close_t	(struct vnode *, int);
typedef ssize_t vop_rdwr_t	(struct vnode *, struct uio *uio);
typedef int 	vop_namei_t	(struct vnode *, const char *, size_t,
					enum vnamei_op op, ino_t *);
typedef int	vop_unlink_t	(struct vnode *, const char *, size_t, int,
					struct vnode *);
typedef int	vop_rename_t	(struct vnode *, const char *, size_t,
					struct vnode *, struct vnode *,
					const char *, size_t, struct vnode *);
typedef int 	vop_create_t	(struct vnode *node, const char *name,
					size_t namelen, vnode_create_args_t *,
					struct vnode **);

/* Getdents returns the numbers of bytes read!
 * On end of directory 0 is returned and on failure an error code is returned
 */
typedef ssize_t vop_getdents_t	(struct vnode *, struct uio *uio);
typedef int	vop_link_t	(struct vnode *, const char *, size_t,
					struct vnode *);
typedef int 	vop_readlink_t	(struct vnode *, struct uio *uio);

/**
 * Calculate the physical block number of a logical block that a vnode uses.
 * If the node contains a hole at this block, 0 is returned in blkno.
 * @return	-EOI 	on error
 *			0		on success
 */
typedef int 	vop_bmap_t	(struct vnode *, bool alloc, blkno_t,
					blkno_t *);
typedef int	vop_sync_t 		(struct vnode *);
typedef int	vop_truncate_t	(struct vnode *, vnode_size_t size);
typedef int 	vop_pagein_t	(struct vnode *node, vm_objoff_t off,
					struct vm_page *page);
typedef int	vop_pageout_t	(struct vnode *, struct vm_page *);
typedef bool	vop_set_exe_t	(struct vnode *node);
typedef void	vop_unset_exe_t	(struct vnode *node);

typedef struct vnode_ops {
	/*
	 * TODO open/close are currently never called...
	 */
	vop_open_t 	*open;
	vop_close_t 	*close;

	vop_rdwr_t 	*read;
	vop_rdwr_t 	*write;

	vop_namei_t	*namei;
	vop_create_t	*create;
	vop_link_t	*link;
	vop_unlink_t	*unlink;
	vop_rename_t	*rename;

	vop_getdents_t	*getdents;
	vop_readlink_t	*readlink;
	vop_truncate_t	*truncate;
	vop_bmap_t	*bmap;
	vop_sync_t	*sync;
	vop_pageout_t	*pageout;
	vop_pagein_t	*pagein;
	vop_set_exe_t	*set_exe;
	vop_unset_exe_t	*unset_exe;
} vnode_ops_t;

/**
 * @brief Used to implement VOP_PANIC
 */
void __noreturn vop_panic(struct vnode *node);

/**
 * @brief Default implementation of the read/write vnode callback.
 *
 * When a filesystem driver decides to use this as the read/write callback it
 * has to implement the bmap and the sync callback. Furthermore this can only be
 * used if the filesystem is backed by a block device.
 */
vop_rdwr_t vop_generic_rdwr;
vop_pagein_t vop_generic_pagein;
vop_pageout_t vop_generic_pageout;
vop_set_exe_t vop_generic_set_exe;
vop_unset_exe_t vop_generic_unset_exe;

#endif
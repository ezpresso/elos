#ifndef VFS_VNODE_H
#define VFS_VNODE_H

/*
 * vnode locking rules
 *
 * Every vnode has two locks, one read-write lock and one mutex
 * from the vm_object of the vnode. The locking rules are
 * described below.
 *
 * The following locks will be held when calling vops:
 *   directory vops:
 *		namei			node->lock (wr)
 *		unlink			node->lock (wr) child->lock (wr)
 *		create			node->lock (wr)
 *		getdents		node->lock (wr)
 *		rename
 *   symlink vops:
 *		readlink		node->lock (wr) TODO consider
 *						changing to rdlock
 *   regular file vops:
 *		read			node->lock (rd)
 *		write			node->lock (wr)
 *		bmap			node->object.lock (node->lock might
 *						also be locked)
 *		truncate		node->lock (wr)
 *		pagein			node->object.lock
 *		pageout			node->object.lock
 *		set_exe			node->lock (wr)
 *		unset_exe		node->lock (wr)
 *   vops for every vnode:
 *		sync			node->statlock (rd)
 *
 * read/write:
 *	A read/write implementation will usually look like this:
 *
 *	wrlock(&node->lock) or rdlock(&node->lock) for the read-callback
 *	loop {
 *		// vnode_getpage handles all of the synchronisation needed
 *		// between read/write and the pageout/pagein callback.
 *		// (the pagein/pageout callbacks do not lock node->lock)
 *		retv = vnode_getpage(node, uio->off & PAGE_MASK, wr or rd,
 *			&page);
 *		if(retv != OK) { ... }
 *
 *		// Copy the data to userspace on read or copy the data into
 *		// the vnode page on write.
 *		res = uiomove_page(page, size, pgoff, uio);
 *		vm_page_unpin(page);
 *		if(res != OK) { ... }
 *	}
 */

#include <kern/atomic.h>
#include <kern/sync.h>
#include <kern/rwlock.h>
#include <lib/list.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <vm/object.h>

struct vm_page;
struct filesys;
struct vnode;
struct dirent;
struct vdirent;
struct uio;
enum vnamei_op;

#define VMTOVN(obj)  container_of(obj, vnode_t, object)
#define VNTOVM(node) (&(node)->object)

#define VN_ASSERT_LOCK_WR(node) rwlock_assert(&(node)->lock, RWLOCK_WR)
#define VN_ASSERT_LOCK_RD(node) rwlock_assert(&(node)->lock, RWLOCK_RD)
#define VN_ASSERT_SLOCK_WR(node) rwlock_assert(&(node)->statlock, RWLOCK_WR)
#define VN_ASSERT_SLOCK_RD(node) rwlock_assert(&(node)->statlock, RWLOCK_RD)
#define VN_ASSERT_LOCK_VM(node) sync_assert(&(node)->object.lock)

/*
 * Flags for vnode_access()
 */
#define VN_ACC_REAL	01
#define VN_ACC_R	0400 /* Has to be same as S_IRUSR */
#define VN_ACC_W	0200 /* Has to be same as S_IWUSR */
#define VN_ACC_X	0100 /* Has to be same as S_IXUSR */
#define VN_ACC_RW	(VN_ACC_R|VN_ACC_W)

/*
 * Used for vnode_settime()
 */
#define VN_ATIME (1 << 0)
#define VN_MTIME (1 << 1)
#define VN_CTIME (1 << 2)
#define VN_CURTIME NULL

/*
 * Used for vnode_lock()
 */
#define VNLOCK_EXCL	0
#define VNLOCK_SHARED	1

#define VN_ISREG(node)  S_ISREG((node)->mode)
#define VN_ISDIR(node)  S_ISDIR((node)->mode)
#define VN_ISLNK(node)  S_ISLNK((node)->mode)
#define VN_ISCHR(node)  S_ISCHR((node)->mode)
#define VN_ISBLK(node)  S_ISBLK((node)->mode)
#define VN_ISFIFO(node) S_ISFIFO((node)->mode)
#define VN_ISSOCK(node) S_ISSOCK((node)->mode)
#define VN_TYPE(node)	((node)->mode & S_IFMT)

#define VN_SIZE_MAX	VM_OBJOFF_MAX

typedef vm_objoff_t vnode_size_t;

#include <vfs/vop.h>

/* TODO add more doc */
typedef struct vnamei_aux {
	/*
	 * Filesystems may initialize off and prev_off in the finddir callback,
	 * so that a subsequent call to unlink knows where to find the dirent
	 * previously looked up. Remember that both finddir and unlink will
	 * be called by vfs code with the node locked, so the values stored
	 * here are safe to use afterwards.
	 */
	blkno_t blkno;
	vnode_size_t off; /* offset of dirent */
	size_t size; /* size of dirent (i.e. rec_len) */
	vnode_size_t prev_off; /* offset of previous dirent */
	size_t prev_size; /* size of previous dirent */

	/*
	 * The biggest hole TODO is it really the biggest hole?
	 */
	vnode_size_t hole_off;
	blkno_t hole_blk;
} vnamei_aux_t;

#define VN_PERM	 (1 << 0) /* vnode_unref is not allowed to free this node */
#define VN_DIRTY (1 << 1) /* some fields (i.e. length, uid, ..) need sync */
#define VN_EXE	 (1 << 2) /* this node is an executable */
#define VN_VCLRU (1 << 3) /* this vnode's reference count is zero and
			   * it is on the vnode-cache's lru.
			   */
typedef uint8_t vnode_flags_t;

typedef struct vnode {
	rwlock_t lock;

	/*
	 * This vm_object is used for vnode-mappings and also acts
	 * as a data cache.
	 */
	struct vm_object object;
	size_t dirty; /* The number of dirty pages. */

	vnode_ops_t *ops;
	struct filesys *fs;
	void *priv; /* filesystem private pointer */
	list_node_t node; /* node for the vnode hashtable of the vcache */
	list_node_t lru_node; /* node for vnode lru list of the vcache */
	vnode_flags_t flags;
	size_t writecnt;

	/*
	 * blksz_shift is constant TODO MOVE TO FS?
	 */
	blksize_t blksz_shift;

	/*
	 * ino and rdev are constant.
	 s*/
	ino_t ino;
	dev_t rdev; /* for device special files */

	rwlock_t statlock;
	vnode_size_t size;
	nlink_t nlink;
	uid_t uid;
	gid_t gid;
	mode_t mode; /* S_IFMT bits can be considered constant */
	blkcnt_t blocks; /* Number of 512 byte blocks allocated */
	struct timespec atime;
	struct timespec mtime;
	struct timespec ctime;
} vnode_t;

extern struct fops vnode_fops;

static inline vnode_flags_t vnode_flags(vnode_t *node) {
	return atomic_load_relaxed(&node->flags);
}

static inline vnode_flags_t vnode_flags_set(vnode_t *node,
	vnode_flags_t flags)
{
	return atomic_or_relaxed(&node->flags, flags);
}

static inline vnode_flags_t vnode_flags_clear(vnode_t *node,
	vnode_flags_t flags)
{
	return atomic_and_relaxed(&node->flags, ~flags);
}

static inline bool vnode_flags_test(vnode_t *node, vnode_flags_t flags) {
	return (atomic_load_relaxed(&node->flags) & flags) == flags;
}

/**
 * @brief Lock a given vnode.
 * @param node	The vnode to be locked.
 * @param lock	Either VNLOCK_EXCL for exclusive access, or VNLOCK_SHARED
 *		which allows to share access between others locking
 *		with VNLOCK_SHARED.
 */
static inline void vnode_lock(vnode_t *node, int lock) {
	if(lock == VNLOCK_EXCL) {
		wrlock(&node->lock);
	} else {
		rdlock(&node->lock);
	}
}

/**
 * @brief Unlock a vnode.
 */
static inline void vnode_unlock(vnode_t *node) {
	rwunlock(&node->lock);
}

/**
 * @brief Initialize a vnode.
 *
 * The fields ino, rdev, dev, length, nlink, uid, gid, mode, blksz_shift,
 * blocks, atim, mtim, ctim and priv are not initialized here. The caller is
 * responsible for initializing them.
 *
 * @param node	The node
 * @param fs	The filesystem the node belongs to.
 * @param ops	The vnode callbacks for this vnode.
 */
void vnode_init(vnode_t *node, struct filesys *fs, vnode_ops_t *ops);

/**
 * @brief Destroy a vnode.
 */
void vnode_destroy(vnode_t *node);

/**
 * @brief Allocates a new vnode and and initializes it (@see vnode_init)
 */
vnode_t *vnode_alloc(struct filesys *fs, vnode_ops_t *ops);

/**
 * @brief Free a vnode. Only called by the vcache.
 */
void vnode_free(vnode_t *node);

/**
 * @brief Write a vnode to disk.
 *
 * Sync the vnode with the inode on the disk. This does not sync the contents
 * of the node. The caller must not hold the statlock.
 */
int vnode_sync(vnode_t *node);

/**
 * @brief Write the cached buffers of a vnode to disk.
 *
 * Schedule the synchronization of the cached pages of a vnode.
 * The caller needs to hold the object lock of the vnode.
 */
void vnode_sched_sync_pages(vnode_t *node);

/**
 * @brief Check if the current process may access this node.
 *
 * @param vnode	The vnode.
 * @param flags	A bitwise combination of VN_ACC_REAL, VN_ACC_R, VN_ACC_W
 *		VN_ACC_X.
 */
bool vnode_access(vnode_t *node, int flags);

/**
 * @brief Read data from a vnode.
 */
ssize_t vnode_read(vnode_t *node, struct uio *uio);

/**
 * @brief Write data to a vnode.
 */
ssize_t vnode_write(vnode_t *node, struct uio *uio);

/**
 * @brief Get the name of a child node of a directory node.
 */
int vnode_childname(vnode_t *parent, ino_t ino, char *buf, size_t bufsz);

/**
 * @brief Read the directory entries of a directory into a buffer.
 */
int vnode_getdents(vnode_t *node, struct uio *uio);

/**
 * @brief Lookup a name in a directory.
 */
int vnode_namei(vnode_t *node, const char *name, size_t namelen, enum vnamei_op,
	ino_t *ino);

/**
 * @brief Perform an unlink operation.
 *
 * @param node		The directory.
 * @param name		The name of the directory entry to be removed.
 * @param namelen	The length of the directory entry name.
 * @param flags		VNAMEI_DIR if the directory entry may point to a non
 *			empty directory or zero if a normal file is unlinked.
 */
int vnode_unlink(vnode_t *node, const char *name, size_t namelen, int flags,
	vnode_t *child);

/**
 * @brief Create a node inside a directory.
 *
 * Creates a new node (regular file, directory, symlink) and a new directory
 * entry to that node in the directory.
 *
 * @param node		The directory.
 * @param name		The name of the new directory entry.
 * @param namelen	The length of the directory entry name.
 * @param args		Various attributes of the new node.
 * @param[out] 		childp The new vnode.
 */
int vnode_create(vnode_t *node, const char *name, size_t namelen,
	vnode_create_args_t *args, vnode_t **childp);

/**
 * @brief Perform a rename operation.
 *
 * Atomically removes the directory entry @p oldname from @p olddir and
 * adds a new directory entry @p newname to @p newdir. If @p newdir
 * already contains a directory entry with that name, @p new is the
 * node that this directory entry points to. If @p new is a directory,
 * it is not allowed to have any directory entries.
 */
int vnode_rename(vnode_t *olddir, const char *oldname, size_t oldlen,
	vnode_t *old, vnode_t *newdir, const char *newname, size_t newlen,
	vnode_t *new);

/**
 * @brief Call the readlink callback of a vnode.
 *
 * TODO move to uio_t
 */
ssize_t vnode_readlink(vnode_t *node, char *buf, size_t size,
	vm_seg_t bufseg);

/**
 * @brief Change the permissions of a vnode.
 */
int vnode_chmod(vnode_t *node, mode_t mode);

/**
 * @brief Change the owner of a vnode.
 */
int vnode_chown(vnode_t *node, uid_t uid, gid_t gid);

/**
 * @brief Set the size of a vnode.
 */
int vnode_truncate(vnode_t *node, vnode_size_t size);

/**
 * @brief Get some information about the vnode in the stat format.
 */
int vnode_stat(vnode_t *node, struct stat64 *stat);

/**
 * @brief Change atime or mtime of a vnode.
 */
int vnode_utimes(vnode_t *node, struct timespec times[2]);

/**
 * @brief Get a page of a vnode.
 *
 * @retval 0
 * @retval -ENOMEM
 * @retval -EIO
 */
int vnode_getpage(vnode_t *node, vm_objoff_t off, vm_flags_t flags,
	struct vm_page **pagep);

/**
 * @brief Map a vnode logical block number to a phyiscal block number.
 */
int vnode_bmap(vnode_t *node, bool alloc, blkno_t blk, blkno_t *pbn);

/**
 * @brief Mark a node as being an executable.
 */
static inline bool vnode_set_exe(vnode_t *node) {
	/*
	 * The node only needs to be read-locked, because
	 * node->flags are always updated atomically.
	 * Furthermore only functions reqiuring
	 * write access will check for the EXE flag
	 * of the vnode and a read-lock prevents
	 * any concurrent calls to such functions.
	 */
	VN_ASSERT_LOCK_RD(node);
	return node->ops->set_exe(node);
}

static inline void vnode_unset_exe(vnode_t *node) {
	VN_ASSERT_LOCK_RD(node);
	node->ops->unset_exe(node);
}

static inline bool vnode_is_exe(vnode_t *node) {
	VN_ASSERT_LOCK_WR(node);
	return vnode_flags_test(node, VN_EXE);
}

/**
 * @brief Check whether a node can be executed.
 */
int vnode_check_exe(vnode_t *node);

/**
 * @brief Reference counting for vnode
 */
static inline vnode_t *vnode_ref(vnode_t *node) {
	vm_object_ref(&node->object);
	return node;
}

/**
 * @brief Reference counting for vnode
 */
static inline void vnode_unref(vnode_t *node) {
	vm_object_unref(&node->object);
}

static inline void *vnode_priv(vnode_t *node) {
	return node->priv;
}

static inline blksize_t vnode_blksz(vnode_t *node) {
	return 1U << node->blksz_shift;
}

/**
 * @brief Set the dirty flag of the vnode.
 *
 * The dirty flag is an indicator, that some fields (like
 * mode, atime, ctime, mtime, ...) are not yet written
 * to disk.
 */
static inline void vnode_dirty(vnode_t *node) {
	VN_ASSERT_SLOCK_WR(node);
	vnode_flags_set(node, VN_DIRTY);
}

/**
 * @brief Change the timestamps of a vnode.
 *
 * Set the atime/ctime/mtime fields of a vnode. The new fields
 * are not synced with the on-disk inode. The caller must hold
 * the statlock of the node for write access.
 *
 * @param node	The vnode.
 * @param ts	A pointer to a timespec describing the time or VN_CURTIME.
 * @param flags A bitwise combination of VN_ATIME, VN_MTIME and VN_CTIME.
 */
void vnode_settime(vnode_t *node, struct timespec *ts, int flags);

static inline void vnode_set_blocks(vnode_t *node, blkcnt_t blks) {
	VN_ASSERT_SLOCK_WR(node);
	node->blocks = blks;
}

void vnode_set_size(vnode_t *node, vnode_size_t size);

static inline void vnode_init_size(vnode_t *node, vnode_size_t size) {
	VNTOVM(node)->size = size;
	node->size = size;
}

static inline void vnode_nlink_inc(vnode_t *node) {
	VN_ASSERT_SLOCK_WR(node);
	node->nlink++;
}

static inline void vnode_nlink_dec(vnode_t *node) {
	VN_ASSERT_SLOCK_WR(node);
	node->nlink--;
}

static inline void vnode_nlink_set(vnode_t *node, nlink_t nlink) {
	VN_ASSERT_SLOCK_WR(node);
	node->nlink = nlink;
}

#endif

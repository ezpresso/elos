#ifndef VFS_FILE_H
#define VFS_FILE_H

#include <kern/atomic.h>
#include <kern/sync.h>
#include <vfs/_vpath.h>
#include <vfs/uio.h>
#include <lib/list.h>
#include <vm/flags.h>
#include <sys/fcntl.h>
#include <sys/poll.h>

#define FACC(flags) ((flags) & O_ACCMODE)
#define FWRITEABLE(flags) (FACC(flags) == O_WRONLY || FACC(flags) == O_RDWR)
/*
 * Have to do an extra check for O_PATH since O_RDONLY is defined as 0. In
 * FWRITEABLE this is unnecessary since O_WRONLY and O_RDWR flags where
 * cleared in kern_open if O_PATH was set.
 */
#define FREADABLE(flags) ((FACC(flags) == O_RDONLY  && !(flags & O_PATH)) || \
	FACC(flags) == O_RDWR)

/*
 * file_t needs 2 byte alignments (which is usually not a problem), because the
 * first bit of a pointer to a file needs to fit the CLOEXEC flag
 * (see vfs/proc.c).
 */
#define FILE_ALIGN 2

struct file;
struct vnode;
struct devfile;
struct polldesc;
struct stat64;
struct vm_object;

typedef struct poll {
	struct polldesc *fds;
	nfds_t nfds;
	int done;
} poll_t;

typedef struct polldesc {
	list_node_t node;
	struct poll *poll;
	struct file *file;
	bool done;
	int fd;
	int events;
	int revents;
} polldesc_t;

typedef int	fop_open_t	(struct file *);
typedef void	fop_close_t	(struct file *);
typedef ssize_t	fop_read_t	(struct file *, uio_t *);
typedef ssize_t	fop_write_t	(struct file *, uio_t *);
typedef int	fop_ioctl_t 	(struct file *, int, void *);
typedef off_t	fop_seek_t 	(struct file *, off_t, int);
typedef void	fop_poll_t	(struct file *, polldesc_t *);
typedef int	fop_stat_t	(struct file *, struct stat64 *stat);
typedef int	fop_mmap_t	(struct file *,  vm_objoff_t off,
	vm_vsize_t size, vm_flags_t *flags, vm_flags_t *max_prot,
	struct vm_object **out);

typedef struct fops {
	fop_open_t	*open;
	fop_close_t	*close;
	fop_read_t	*read;
	fop_write_t	*write;
	fop_ioctl_t	*ioctl;
	fop_seek_t	*seek;
	fop_poll_t	*poll;
	fop_stat_t	*stat;
	fop_mmap_t	*mmap;
} fops_t;

typedef enum ftype {
	FUNK = 0,
	FREG,
	FDIR,
	FDEV,
	FSOCK,
	FPIPE,
} ftype_t;

typedef struct file {
	ref_t ref;
	int flags; /* file open flags */
	fops_t *fops;
	void *priv; /* private pointer */
	ftype_t type;

#define FOFF_LOCKED	(1 << 0)
#define FOFF_WAITING	(1 << 1)
	uint8_t foff_flags;
	off_t foff;

	sync_t pollq_lock;
	list_t pollq;
	vpath_t path;

	/*
	 * Maybe unionize with foff?
	 */
	struct dev_ctx *dev;
} file_t;

/**
 * @brief Reference counting for files
 */
static inline file_t *file_ref(file_t *file) {
	ref_inc(&file->ref);
	return file;
}

/**
 * @brief Call the open callback of a file
 */
static inline int file_open(file_t *file) {
	assert(file->fops->open);
	return file->fops->open(file);
}

/**
 * @brief Call the close callback of a file
 */
static inline void file_close(file_t *file) {
	if(file->fops->close) {
		file->fops->close(file);
	}
}

/**
 * @brief Check if a file is writable.
 */
static inline bool file_writeable(file_t *file) {
	return FWRITEABLE(file->flags);
}

/**
 * @brief Check if a file is readable.
 */
static inline bool file_readable(file_t *file) {
	return FREADABLE(file->flags);
}

/**
 * @brief Call the write callback of a file
 */
static inline ssize_t file_write(file_t *file, uio_t *uio) {
	if(!file_writeable(file)) {
		return -EBADF;
	} else if(uio->size == 0) {
		return 0;
	} else {
		/*
		 * If the file could be opened with write access
		 * then there should be a write callback.
		 */
		assert(file->fops->write);
		return file->fops->write(file, uio);
	}
}

/**
 * @brief Call the read callback of a file
 */
static inline ssize_t file_read(file_t *file, uio_t *uio) {
	if(!file_readable(file)) {
		return -EBADF;
	} else if(uio->size == 0) {
		return 0;
	} else {
		/*
		 * If the file could be opened with read access
		 * then there should be a write callback.
		 */
		assert(file->fops->read);
		return file->fops->read(file, uio);
	}
}

/**
 * @brief Call the ioctl callback of a file.
 */
static inline int file_ioctl(file_t *file, int cmd, void *arg) {
	if(file->flags & O_PATH) {
		return -EBADF;
	} else if(!file->fops->ioctl) {
		return -ENOTTY;
	} else {
		return file->fops->ioctl(file, cmd, arg);
	}
}

/**
 * @brief Call the seek callback of a file.
 */
static inline off_t file_seek(file_t *file, off_t off, int whence) {
	if(!file->fops->seek) {
		return -ESPIPE;
	} else {
		return file->fops->seek(file, off, whence);
	}
}

/**
 * @brief Call the poll callback of a file.
 */
static inline void file_poll(file_t *file, polldesc_t *desc) {
	assert(file->fops->poll);
	file->fops->poll(file, desc);
}

/**
 * @brief Call the stat callback of a file.
 */
static inline int file_stat(file_t *file, struct stat64 *stat) {
	assert(file->fops->stat);
	return file->fops->stat(file, stat);
}

/**
 * @brief Call the mmap callback of a file.
 */
int file_mmap(file_t *file, vm_objoff_t off, vm_vsize_t size,
	vm_flags_t *flagsp, vm_flags_t *max_prot, struct vm_object **out);

/**
 * Set the private pointer of a file. Panics if there
 * is already a private pointer.
 */
static inline void file_set_priv(file_t *file, void *priv) {
	if(file->priv && priv != NULL) {
		kpanic("[file] Multiple file private pointers");
	} else {
		file->priv = priv;
	}
}

/**
 * @brief Get the private pointer of a file.
 */
static inline void *file_get_priv(file_t *file) {
	return file->priv;
}

/**
 * @brief Get the file offset of a file.
 *
 * Returns the file offset from a file without locking
 * the file offset.
 *
 */
static inline off_t foff_get(file_t *file) {
	return atomic_load(&file->foff);
}

/**
 * Allocate a new file structure. If the file is not needed anymore
 * you will have to call file_unref() with the file as an argument.
 */
int file_alloc(ftype_t type, int flags, fops_t *ops, file_t **fp);

/**
 * @brief Allocate a new file structure for a path.
 */
int file_alloc_path(vpath_t *path, int flags, file_t **fp);

/**
 * @brief Get the vnode of a file
 *
 * Get the vnode of a file, which might be NULL for some pipes. This
 * function does not increment the vnode's reference count.
 */
struct vnode *file_vnode(file_t *file);

/**
 * @brief Get the vpath of a file. May be NULL.
 */
vpath_t *file_vpath(file_t *file);

/**
 * @brief Check whether a file belongs to a specific device special file.
 */
bool file_is_dev(file_t *file, dev_t dev);

/**
 * Free a file allocated using file_alloc or
 * file_alloc_path. The open callback of the
 * file may not have been called yet.
 */
void file_free(file_t *file);

/**
 * @brief Drop the reference count of a file.
 */
void file_unref(file_t *file);

/**
 * Get the current file offset and prevent the offset
 * from being changed. Once the read/write/seek operation
 * finished and the file offset has to be incremented (changed)
 * foff_unlock must be called.
 */
off_t foff_lock_get(file_t *file);

/**
 * Seth the file offset to @p off and unlock the file offset,
 * which was locked using foff_lock_get.
 */
void foff_unlock(file_t *file, off_t off);

/**
 * Locks the file offset and sets uio->off to the correct
 * value if needed.
 */
void foff_lock_get_uio(file_t *file, uio_t *uio);

/**
 * Saves the new offset uio->off in the file and unlocks the
 * file offset if needed.
 */
void foff_unlock_uio(file_t *file, uio_t *uio);

#endif

/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 *
 * Copyright (c) 2018, Elias Zell
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <kern/system.h>
#include <kern/sync.h>
#include <kern/symbol.h>
#include <kern/futex.h>
#include <vm/malloc.h>
#include <vfs/vfs.h>
#include <vfs/file.h>
#include <vfs/uio.h>
#include <vfs/dev.h>
#include <vfs/devfs.h>
#include <vfs/vnode.h>
#include <lib/string.h>
#include <lib/list.h>

#define DF_DEAD		(1 << 0)
#define DF_READONLY	(1 << 1)

typedef struct dev_file {
	struct devfs_node *devfs;
	fops_t *fops;
	dev_t dev;
	void *priv;

	sync_t lock;
	size_t usage;
	size_t wrcnt;
	int flags;
	list_t ctx_list;
} dev_file_t;

typedef struct dev_ctx {
	sync_t lock;
	dev_file_t *df;
	list_node_t node;
	file_t *file;
} dev_ctx_t;

static fop_open_t dev_open;
static fop_close_t dev_close;
static fop_write_t dev_write;
static fop_read_t dev_read;
static fop_ioctl_t dev_ioctl;
static fop_stat_t dev_stat;
static fop_mmap_t dev_mmap;
fops_t dev_ops = {
	.open = dev_open,
	.close = dev_close,
	.write = dev_write,
	.read = dev_read,
	.ioctl = dev_ioctl,
	.stat = dev_stat,
	.mmap = dev_mmap,
};

static dev_file_t *dev_files[NMAJOR][MINOR_MAX];
static sync_t dev_lock = SYNC_INIT(MUTEX);

/**
 * @brief Allocate a minor number for a device special file.
 *
 * Allocate a minor number for a device special file. The combination
 * of the major number and the minor number of a device special
 * file is considered uniquie.
 */
static bool dev_alloc_minor(unsigned major, dev_file_t *df) {
	size_t i;

	/*
	 * Don't allow device number 0.
	 */
	i = major == 0 ? 1 : 0;

	sync_scope_acquire(&dev_lock);
	for(; i < MINOR_MAX; i++) {
		if(dev_files[major][i] == NULL) {
			dev_files[major][i] = df;
			df->dev = makedev(major, i);
			return true;
		}
	}

	return false;
}

static dev_file_t *dev_file_alloc(void) {
	dev_file_t *df;

	df = kmalloc(sizeof(*df), VM_WAIT);
	sync_init(&df->lock, SYNC_MUTEX);
	list_init(&df->ctx_list);
	df->flags = 0;
	df->usage = 0;
	df->wrcnt = 0;

	return df;
}

static void dev_file_free(dev_file_t *df) {
	sync_destroy(&df->lock);
	list_destroy(&df->ctx_list);
	kfree(df);
}

static dev_ctx_t *dev_ctx_alloc(dev_file_t *df, file_t *file) {
	dev_ctx_t *ctx;

	ctx = kmalloc(sizeof(*ctx), VM_WAIT);
	list_node_init(ctx, &ctx->node);
	sync_init(&ctx->lock, SYNC_MUTEX);
	ctx->df = df;
	ctx->file = file;
	file->dev = ctx;

	assert(file->priv == NULL);
	file_set_priv(file, df->priv);

	return ctx;
}

static void dev_ctx_free(dev_ctx_t *ctx) {
	list_node_destroy(&ctx->node);
	sync_destroy(&ctx->lock);
	kfree(ctx);
}

static int dev_file_ref(dev_file_t *df) {
	sync_scope_acquire(&df->lock);
	if(F_ISSET(df->flags, DF_DEAD)) {
		return -ENXIO;
	} else {
		df->usage++;
		return 0;
	}
}

static void dev_file_unref(dev_file_t *df) {
	bool wake;

	sync_acquire(&df->lock);
	assert(df->usage);
	wake = --df->usage == 0 && F_ISSET(df->flags, DF_DEAD);
	sync_release(&df->lock);

	if(wake) {
		kern_wake(&df->usage, 1, 0);
	}
}

/**
 * @brief Prepare a new filesystem operation on a device special file.
 * @param	file The file descriptor of the opened device file.
 * @param[out]	dfp  The device file private structure of the file.
 * @retval -ENXIO	The device file was removed and this the operation
 *			should be aborted.
 * @retval 0		The device file is guaranteed to be valid until
 *			dev_fop_end. This means that the file-op of the
 *			device can safely be called.
 */
static int dev_fop_start(file_t *file, dev_file_t **dfp) {
	dev_ctx_t *ctx = file->dev;

	sync_scope_acquire(&ctx->lock);
	if(ctx->df == NULL) {
		return -ENXIO;
	} else {
		return *dfp = ctx->df, dev_file_ref(ctx->df);
	}
}

/**
 * @brief Mark the end of a device file operation.
 *
 * Mark the end of a device file operation. It is not safe to use
 * dev_file after calling this function.
 */
static void dev_fop_end(dev_file_t *df) {
	dev_file_unref(df);
}

/**
 * @brief Close an dev file.
 *
 * Call the close callback of the device special file. The close
 * callback is usually called when a process closes its fd for
 * the device file. However the device might be removed while
 * there are still some file-descriptors open for the file
 * and thus the close callback might have to be called before
 * the real file_close. In this case file_close is a noop.
 */
static void dev_do_close(file_t *file, dev_file_t *df) {
	if(df->fops->close) {
		df->fops->close(file);
	}

	if(FWRITEABLE(file->flags)) {
		sync_scope_acquire(&df->lock);
		assert(df->wrcnt);
		df->wrcnt--;
	}
}

static int dev_open(file_t *file) {
	unsigned maj, min;
	dev_file_t *df;
	dev_ctx_t *ctx;
	dev_t dev;
	int err;

	dev = file_vnode(file)->rdev;
	maj = major(dev);
	min = minor(dev);
	if(maj >= NMAJOR || min >= MINOR_MAX) {
		return -ENXIO;
	}

	synchronized(&dev_lock) {
		df = dev_files[maj][min];
		if(df == NULL) {
			return -ENXIO;
		} else if(FWRITEABLE(file->flags) &&
			F_ISSET(df->flags, DF_READONLY))
		{
			return -EACCES;
		}

		if(dev_file_ref(df) < 0) {
			return -ENXIO;
		}

		if(FWRITEABLE(file->flags)) {
			df->wrcnt++;
		}
	}

	ctx = dev_ctx_alloc(df, file);
	err = df->fops->open(file);
	if(err) {
		dev_ctx_free(ctx);
	} else {
		sync_scope_acquire(&df->lock);
		list_append(&df->ctx_list, &ctx->node);
	}

	dev_file_unref(df);
	return err;
}

static ssize_t dev_read(file_t *file, struct uio *uio) {
	dev_file_t *df;
	ssize_t ret;

	ret = dev_fop_start(file, &df);
	if(ret == 0) {
		ret = df->fops->read(file, uio);
		dev_fop_end(df);
	}

	return ret;
}

static ssize_t dev_write(file_t *file, struct uio *uio) {
	dev_file_t *df;
	ssize_t ret;

	ret = dev_fop_start(file, &df);
	if(ret == 0) {
		ret = df->fops->write(file, uio);
		dev_fop_end(df);
	}

	return ret;
}

static ssize_t dev_ioctl(file_t *file, int cmd, void *arg) {
	dev_file_t *df;
	int err;

	err = dev_fop_start(file, &df);
	if(err == 0) {
		if(df->fops->ioctl) {
			err = df->fops->ioctl(file, cmd, arg);
		} else {
			err = -ENOTTY;
		}

		dev_fop_end(df);
	}

	return err;
}

static int dev_stat(file_t *file, struct stat64 *stat) {
	return vnode_fops.stat(file, stat);
}

static int dev_mmap(file_t *file, vm_objoff_t off, vm_vsize_t size,
	vm_flags_t *flags, vm_flags_t *max_prot, struct vm_object **out)
{
	dev_file_t *df;
	int err;

	err = dev_fop_start(file, &df);
	if(err == 0) {
		if(df->fops->mmap) {
			err = df->fops->mmap(file, off, size, flags, max_prot,
				out);
		} else {
			err = -EACCES;
		}

		dev_fop_end(df);
	}

	return err;
}

static void dev_close(file_t *file) {
	dev_ctx_t *ctx;
	dev_file_t *df;
	int err;

	ctx = file->dev;
	err = dev_fop_start(file, &df);

	/*
	 * If an dev_fop_start returns an error value, destroydev() has
	 * already called or will call the close callback of the device.
	 */
	if(err == 0) {
		dev_do_close(file, df);
		synchronized(&df->lock) {
			list_remove(&df->ctx_list, &ctx->node);
		}
		dev_fop_end(df);
	}

	dev_ctx_free(ctx);
}

bool dev_file_cmp(file_t *file, dev_t dev) {
	dev_file_t *df;
	bool ret;
	int err;

	err = dev_fop_start(file, &df);
	if(err) {
		return false;
	} else {
		ret = df->dev == dev;
		dev_fop_end(df);
		return ret;
	}
}

bool dev_match_fops(file_t *file, fops_t *fops) {
	dev_file_t *df;
	bool ret;
	int err;

	err = dev_fop_start(file, &df);
	if(err) {
		return false;
	} else {
		ret = df->fops == fops;
		dev_fop_end(df);
		return ret;
	}
}

static int vmakedev(struct devfs_node *node, unsigned maj, mode_t mode,
	size_t blksz_shift, fops_t *fops, void *priv, dev_t *res,
	const char *name, va_list ap)
{
	char namebuf[32];
	dev_file_t *df;

	assert(fops);
	df = dev_file_alloc();
	df->fops = fops;
	df->priv = priv;

	vsnprintf(namebuf, sizeof(namebuf), name, ap);
	if(!dev_alloc_minor(maj, df)) {
		/*
		 * TODO Improve minor number allocation!
		 */
		kpanic("[devfile] could not create: \"%s\"", namebuf);
	}

	/*
	 * Create the devfs entry in the /dev mount.
	 */
	df->devfs = devfs_mknodat(node, df->dev, mode, blksz_shift, namebuf);
	if(res) {
		*res = df->dev;
	}

	return 0;
}

int vmakechar(struct devfs_node *node, unsigned maj, mode_t mode,
	fops_t *fops, void *priv, dev_t *res, const char *name, va_list ap)
{
	assert((mode & S_IFMT) == 0);
	return vmakedev(node, maj, mode | S_IFCHR, 0, fops, priv, res, name,
		ap);
}
export(vmakechar);

int makechar(struct devfs_node *node, unsigned maj, mode_t mode,
	fops_t *fops, void *priv, dev_t *res, const char *name, ...)
{
	va_list ap;
	int retv;

	va_start(ap, name);
	retv = vmakechar(node, maj, mode, fops, priv, res, name, ap);
	va_end(ap);

	return retv;
}
export(makechar);

int vmakeblk(struct devfs_node *node, unsigned maj, mode_t mode,
	size_t blksz_shift, fops_t *fops, void *priv, dev_t *res,
	const char *name, va_list ap)
{
	assert((mode & S_IFMT) == 0);
	return vmakedev(node, maj, mode | S_IFBLK, blksz_shift, fops, priv,
		res, name, ap);
}
export(vmakeblk);

int makeblk(struct devfs_node *node, unsigned maj, mode_t mode,
	size_t blksz_shift, fops_t *fops, void *priv, dev_t *res,
	const char *name, ...)
{
	va_list ap;
	int retv;

	va_start(ap, name);
	retv = vmakeblk(node, maj, mode, blksz_shift, fops, priv, res, name,
		ap);
	va_end(ap);

	return retv;
}
export(makeblk);

void destroydev(dev_t dev) {
	unsigned maj, min;
	dev_ctx_t *ctx;
	dev_file_t *df;
	size_t usage;

	maj = major(dev);
	min = minor(dev);
	assert(maj < NMAJOR && min < MINOR_MAX);

	df = dev_files[maj][min];
	assert(df);

	/*
	 * Remove the devfs entry of the device file, so that no one
	 * can open the file anymore.
	 */
	devfs_rmnod(df->devfs);

	/*
	 * Prevent threads from starting new callbacks.
	 */
	sync_acquire(&df->lock);
	F_SET(df->flags, DF_DEAD);

	/*
	 * Wait until all currently running callbacks have finished.
	 */
	usage = df->usage;
	sync_release(&df->lock);
	if(usage != 0) {
		kern_wait(&df->usage, usage, 0);
		assert(df->usage == 0);
	}

	/*
	 * Call the close callback for every open() of the device file. The
	 * actual dev_ctx_t structures will be freed during the close()
	 * syscall.
	 */
	while((ctx = list_pop_front(&df->ctx_list))) {
		synchronized(&ctx->lock) {
			ctx->df = NULL;
		}

		dev_do_close(ctx->file, df);
	}

	assert(df->wrcnt == 0);

	/*
	 * Free the minor device number.
	 */
	synchronized(&dev_lock) {
		dev_files[maj][min] = NULL;
	}

	/*
	 * It is now safe to free the device file, because nobody
	 * can have a reference to it anympre.
	 */
	dev_file_free(df);
}

int dev_prot(dev_t dev) {
	unsigned maj, min;
	dev_file_t *df;

	maj = major(dev);
	min = minor(dev);
	assert(maj < NMAJOR && min < MINOR_MAX);

	sync_scope_acquire(&dev_lock);
	df = dev_files[maj][min];
	assert(df);

	sync_scope_acquire(&df->lock);
	assert(!F_ISSET(df->flags, DF_DEAD));
	assert(!F_ISSET(df->flags, DF_READONLY));

	/*
	 * If the device file is currently opened for writing,
	 * it cannot be write-protected.
	 */
	if(df->wrcnt) {
		return -EBUSY;
	} else {
		F_SET(df->flags, DF_READONLY);
		return 0;
	}
}

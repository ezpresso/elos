/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 *
 * Copyright (c) 2017, Elias Zell
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
#include <kern/futex.h>
#include <vfs/file.h>
#include <vfs/vnode.h>
#include <vfs/vpath.h>
#include <vfs/dev.h>
#include <vm/slab.h>
#include <vm/flags.h>
#include <sys/limits.h>
#include <sys/stat.h>

static DEFINE_VM_SLAB(file_cache, sizeof(file_t), FILE_ALIGN);

static inline int file_do_alloc(vpath_t *path, int flags, file_t **fp) {
	ftype_t type = FUNK;
	file_t *file;

	file = vm_slab_alloc(&file_cache, 0);
	if(unlikely(!file)) {
		return *fp = NULL, -ENOMEM;
	}

	ref_init(&file->ref);
	vpath_init(&file->path);
	file->flags = flags;
	file->foff_flags = 0;
	file->foff = 0;
	file->priv = NULL;

	if(path != NULL) {
		vpath_cpy(&file->path, path);

		/*
		 * Associate the correct file operations
		 * with a file, based on the underlying
		 * vnode type.
		 */
		switch(VN_TYPE(path->node)) {
		case S_IFREG:
		case S_IFLNK:
			file->fops = &vnode_fops;
			type = FREG;
			break;
		case S_IFDIR:
			file->fops = &vnode_fops;
			type = FDIR;
			break;
		case S_IFCHR:
		case S_IFBLK:
			file->fops = &dev_ops;
			type = FDEV;
			break;
		case S_IFIFO:
		case S_IFSOCK:
			kpanic("[file] TODO fifo/socket");
			break;
		default:
			kpanic("[file] invalid vnode mode: %d\n",
				VN_TYPE(path->node));
		}
	}

	file->type = type;
	return *fp = file, 0;
}

int file_alloc(ftype_t type, int flags, fops_t *ops, file_t **fp) {
	file_t *file;
	int err;

	err = file_do_alloc(NULL, flags, &file);
	if(likely(!err)) {
		file->fops = ops;
		file->type = type;
	}

	return *fp = file, err;
}

int file_alloc_path(vpath_t *path, int flags, file_t **fp) {
	return file_do_alloc(path, flags, fp);
}

vnode_t *file_vnode(file_t *file) {
	/*
	 * FPIPE may or may not have a vnode backend.
	 */
	return file->path.node;
}

vpath_t *file_vpath(file_t *file) {
	if(file->path.node) {
		return &file->path;
	} else {
		return NULL;
	}
}

bool file_is_dev(file_t *file, dev_t dev) {
	return file->type == FDEV && dev_file_cmp(file, dev);
}

void file_free(file_t *file) {
	vpath_clear(&file->path);
	vm_slab_free(&file_cache, file);
}

void file_unref(file_t *file) {
	if(ref_dec(&file->ref)) {
		/*
		 * Call the close callback before
		 * freeing the file.
		 */
		file_close(file);
		file_free(file);
	}
}

int file_mmap(file_t *file, vm_objoff_t off, vm_vsize_t size,
	vm_flags_t *flagsp, vm_flags_t *max_prot, struct vm_object **out)
{
	vm_flags_t flags = *flagsp;

	/*
	 * Not checking for rw-permissions here, because a private vnode
	 * mapping may have write access even though the file descriptor
	 * is read only (i.e. the rw-permissons depend on the file descriptor
	 * type).
	 */
	if(file->type == FDIR || !FREADABLE(file->flags) ||
		(VM_PROT_WR_P(flags) && (file->flags & O_APPEND)))
	{
		return -EACCES;
	} else if(VM_PROT_EXEC_P(flags)) {
		assert(!FWRITEABLE(file->flags));
		/* if(!(file->flags & O_EXEC)) {
			return -EACCES;
		} */
	}

	*max_prot = VM_PROT_RD;
	if(FWRITEABLE(file->flags)) {
		*max_prot |= VM_PROT_WR;
	} else if(file->flags & O_EXEC) {
		*max_prot |= VM_PROT_EXEC;
	}

	assert(file->fops->mmap);
	return file->fops->mmap(file, off, size, flagsp, max_prot, out);
}

off_t foff_lock_get(file_t *file) {
	uint8_t flags;

	/*
	 * Set the FOFF_LOCKED flag. If it was already set before,
	 * the thread will have to wait.
	 */
	while((flags = atomic_or(&file->foff_flags, FOFF_LOCKED)) &
		FOFF_LOCKED)
	{
		/*
		 * Set the FOFF_WAITING flag so that the next call to
		 * foff_unlock will wake the thread up again. Don't sleep, if
		 * the cmpxchg operation fails, because the the file offset
		 * might have been unlocked again.
		 */
		if(atomic_cmpxchg(&file->foff_flags, flags,
			flags | FOFF_WAITING) == true)
		{
			flags |= FOFF_WAITING;
			kern_wait(&file->foff_flags, flags, 0);
		}
	}

	return file->foff;
}

void foff_unlock(file_t *file, off_t off) {
	assert(off >= 0);

	file->foff = off;
	if(atomic_xchg(&file->foff_flags, 0) & FOFF_WAITING) {
		kern_wake(&file->foff_flags, INT_MAX, 0);
	}
}

void foff_lock_get_uio(file_t *file, uio_t *uio) {
	/*
	 * If no offset was provided by the uio, use and update
	 * the file offset.
	 */
	if(!F_ISSET(uio->flags, UIO_OFF)) {
		uio->off = foff_lock_get(file);
	}
}

void foff_unlock_uio(file_t *file, uio_t *uio) {
	if(!F_ISSET(uio->flags, UIO_OFF)) {
		foff_unlock(file, uio->off);
	}
}

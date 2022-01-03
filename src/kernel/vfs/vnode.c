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
 * purpose with or without fee is hereby granted, proided that the above
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
#include <kern/proc.h>
#include <kern/time.h>
#include <vfs/vnode.h>
#include <vfs/file.h>
#include <vfs/fs.h>
#include <vfs/vcache.h>
#include <vfs/vdirent.h>
#include <vfs/uio.h>
#include <vfs/lookup.h>
#include <vm/malloc.h>
#include <vm/slab.h>
#include <vm/object.h>
#include <vm/page.h>
#include <vm/vas.h> /* VM_MAP_PRIV_P TODO move to vm/map.h */
#include <vm/pageout.h>
#include <vm/pager.h>
#include <block/block.h>
#include <lib/string.h>
#include <sys/poll.h>
#include <sys/dirent.h>
#include <sys/limits.h>

static DEFINE_VM_SLAB(vnode_cache, sizeof(vnode_t), 0);
static vm_obj_map_t	vnode_vm_map;
static vm_obj_fault_t	vnode_vm_fault;
static vm_obj_destroy_t	vnode_vm_destroy;
static vm_obj_dirty_t	vnode_vm_dirty;
vm_obj_ops_t vm_vnode_ops = {
	.map =		vnode_vm_map,
	.fault =	vnode_vm_fault,
	.dirty =	vnode_vm_dirty,
	.destroy =	vnode_vm_destroy,
};

static vm_pager_pagein_t vnode_pagein;
static vm_pager_pageout_t vnode_pageout;
static vm_pager_t vnode_pager = {
	.pagein = vnode_pagein,
	.pageout = vnode_pageout,
};

static fop_open_t vnode_fop_open;
static fop_close_t vnode_fop_close;
static fop_read_t vnode_fop_rdwr;
static fop_seek_t vnode_fop_seek;
static fop_stat_t vnode_fop_stat;
static fop_mmap_t vnode_fop_mmap;
static fop_poll_t vnode_fop_poll;
fops_t vnode_fops = {
	.open = vnode_fop_open,
	.close = vnode_fop_close,
	.write = vnode_fop_rdwr,
	.read = vnode_fop_rdwr,
	.poll = vnode_fop_poll,
	.seek = vnode_fop_seek,
	.stat = vnode_fop_stat,
	.mmap = vnode_fop_mmap,
};

/**
 * @brief Validate vnode callbacks.
 */
static void vnode_ops_check(vnode_ops_t *ops) {
	assert(ops->read);
	assert(ops->write);
	assert(ops->truncate);
	assert(ops->namei);
	assert(ops->create);
	assert(ops->unlink);
	assert(ops->rename);
	assert(ops->getdents);
	/* TODO assert(node->link); */
	assert(ops->readlink);
	assert(ops->sync);
	assert(ops->bmap);
	assert(ops->pagein);
	assert(ops->pageout);
	assert(ops->set_exe);
	assert(ops->unset_exe);
}

void vnode_init(vnode_t *node, filesys_t *fs, vnode_ops_t *ops) {
	vnode_ops_check(ops);

	vm_object_init(&node->object, 0, &vm_vnode_ops, &vnode_pager);
	list_node_init(node, &node->node);
	list_node_init(node, &node->lru_node);
	rwlock_init(&node->statlock);
	rwlock_init(&node->lock);
	node->fs = filesys_ref(fs);
	node->ops = ops;
	node->priv = NULL;
	node->flags = 0;
	node->dirty = 0;
	node->writecnt = 0;
	node->size = 0;
}

void vnode_destroy(vnode_t *node) {
	synchronized(&node->object.lock) {
		vm_object_dead(&node->object);
		vm_object_clear(&node->object);
	}

	list_destroy(&node->object.maps); /* XXX */
	vm_object_destroy(&node->object);
	filesys_unref(node->fs);

	rwlock_destroy(&node->statlock);
	rwlock_destroy(&node->lock);
	list_node_destroy(&node->node);
	list_node_destroy(&node->lru_node);
}

vnode_t *vnode_alloc(filesys_t *fs, vnode_ops_t *ops) {
	vnode_t *node;

	node = vm_slab_alloc(&vnode_cache, VM_NOFLAG);
	if(node) {
		vnode_init(node, fs, ops);
	}

	return node;
}

void vnode_free(vnode_t *node) {
	vnode_destroy(node);
	vm_slab_free(&vnode_cache, node);
}

int vnode_sync(vnode_t *node) {
	int err = 0;

	/*
	 * Make sure that the vnode's attributes do not change.
	 */
	rdlock_scope(&node->statlock);

	/*
	 * Only try syncing the node if the node actually needs to
	 * be synced.
	 */
	if(vnode_flags_clear(node, VN_DIRTY) & VN_DIRTY) {
		err = node->ops->sync(node);
	}

	return err;
}

void vnode_sched_sync_pages(vnode_t *node) {
	vm_object_t *object = VNTOVM(node);
	vm_page_t *page;

	sync_assert(&object->lock);
	assert(node->dirty);

	foreach(page, &object->pages) {
		if(vm_page_is_dirty(page)) {
			vm_page_pin(page);
			sync_release(&object->lock);
			vm_sync_needed(page, VM_SYNC_NOW);
			vm_page_unpin(page);
			sync_acquire(&object->lock);
		}
	}
}

bool vnode_access(vnode_t *node, int flags) {
	mode_t mode;
	uid_t uid;
	gid_t gid;

	/* TODO VN_ASSERT_SLOCK_RD(node); */

	if(flags & VN_ACC_REAL) {
		proc_get_ids(&uid, &gid);
	} else {
		proc_get_eids(&uid, &gid);
	}

	/*
	 * root user / kernel process
	 */
	if(uid == UID_ROOT) {
		return true;
	}

	/*
	 * This is possible because VN_ACC_R is equal to S_IRUSR and
	 * VN_ACC_W is equal to VN_ACC_W and VN_ACC_X is equal to IS_IXUSR.
	 */
	mode = flags & ~VN_ACC_REAL;
	if(uid == node->uid && (node->mode & mode) == mode) {
		return true;
	}

	mode >>= 3;
	if(gid == node->gid && (node->mode & mode) == mode) {
		return true;
	}

	mode >>= 3;
	return (node->mode & mode) == mode;
}

void vnode_set_size(vnode_t *node, vnode_size_t size) {
	VN_ASSERT_LOCK_WR(node);

	synchronized(&VNTOVM(node)->lock) {
		vm_object_resize(VNTOVM(node), size);
	}

	wrlocked(&node->statlock) {
		node->size = size;
		vnode_dirty(node);
	}
}

void vnode_settime(vnode_t *node, struct timespec *ts, int flags) {
	struct timespec cur;

	VN_ASSERT_SLOCK_WR(node);
	if(ts == VN_CURTIME) {
		realtime(&cur);
		ts = &cur;
	}

	if(F_ISSET(flags, VN_ATIME)) {
		node->atime = *ts;
	}
	if(F_ISSET(flags, VN_MTIME)) {
		node->mtime = *ts;
	}
	if(F_ISSET(flags, VN_CTIME)) {
		node->ctime = *ts;
	}
}

int vnode_check_exe(vnode_t *node) {
	vm_object_t *object = VNTOVM(node);
	vm_map_t *map;

	VN_ASSERT_LOCK_RD(node);

	/*
	 * An empty file cannot be executed.
	 */
	if(node->size == 0) {
		return -ENOEXEC;
	} else if(!VN_ISREG(node)) {
		return -EACCES;
	} else if(vnode_access(node, VN_ACC_X) == false) {
		return -EACCES;
	} else if(node->writecnt) {
		return -ETXTBSY;
	}

	sync_scope_acquire(&object->lock);
	foreach(map, &object->maps) {
		sync_scope_acquire(&map->lock);
		if(VM_MAP_SHARED_P(map->flags) && VM_PROT_WR_P(map->max_prot)) {
			return -ETXTBSY;
		}
	}

	return 0;
}

int vnode_getdents(vnode_t *node, uio_t *uio) {
	assert(uio->rw == UIO_RD);
	if(!VN_ISDIR(node)) {
		return -ENOTDIR;
	}

	wrlock_scope(&node->lock);
	if(!vnode_access(node, VN_ACC_R)) {
		return -EACCES;
	} else if(node->nlink == 0) {
		/*
		 * Directories that have been deleted don't contain
		 * and dirents.
		 */
		return 0;
	} else {
		return node->ops->getdents(node, uio);
	}
}

int vnode_childname(vnode_t *parent, ino_t ino, char *namebuf, size_t nbufsz) {
	const size_t bufsz = NAME_MAX + 1;
	struct iovec iov;
	off_t off = 0;
	void *buf;
	uio_t uio;
	int res = 0;

	if(!VN_ISDIR(parent)) {
		return -ENOTDIR;
	}

	buf = kmalloc(bufsz, VM_NOFLAG);
	if(!buf) {
		return -ENOMEM;
	}

	vnode_lock(parent, VNLOCK_EXCL);
	for(;;) {
		ssize_t len;
		size_t d_off;

		iov.iov_len = bufsz;
		iov.iov_base = buf;
		uio_simple(&uio, &iov, off, UIO_KERN, UIO_RD);

		len = parent->ops->getdents(parent, &uio);
		if(len < 0) {
			res = len;
			goto out;
		} else if(len == 0) {
			res = -ENOENT;
			goto out;
		}

		d_off = 0;
		while(d_off < (size_t)len) {
			struct dirent *dent = buf + d_off;
			if(dent->d_ino == ino) {
				/*
				 * Dirents are null-terminated, so
				 * cannot use strncpy here.
				 */
				strlcpy(namebuf, dent->d_name, nbufsz);
				goto out;
			}

			d_off += dent->d_reclen;
		}

		off = uio.off;
	}

out:
	vnode_unlock(parent);
	kfree(buf);
	return res;
}

#if notyet
int vnode_link(vnode_t *node, const char *name, size_t namelen,
		vnode_t *target)
{
	sync_assert(&node->lock);
	sync_assert(&target->lock);

	if(!VN_ISDIR(node)) {
		return -ENOTDIR;
	} else if(VN_ISDIR(target->mode)) {
		return -EPERM;
	/* TODO should have been checked before */
	} else if(node->fs != target->fs) {
		return -EXDEV;
	} else {
		return node->ops->link(node, name, namelen, target);
	}
}
#endif

int vnode_namei(vnode_t *node, const char *name, size_t namelen,
	vnamei_op_t op, ino_t *ino)
{
	VN_ASSERT_LOCK_WR(node);
	assert(VN_ISDIR(node));

	if(node->nlink == 0) {
		/*
		 * -ENOENT is valid for directories which are
		 * unlinked because rmdir can only be called on
		 * empty directories.
		 */
		return -ENOENT;
	} else {
		return node->ops->namei(node, name, namelen, op, ino);
	}
}

int vnode_create(vnode_t *node, const char *name, size_t namelen,
	vnode_create_args_t *args, vnode_t **childp)
{
	VN_ASSERT_LOCK_WR(node);
	assert(VN_ISDIR(node));
	assert(args);

	if(node->nlink == 0) {
		/*
		 * Cannot create a new dirent in a deleted directory.
		 */
		return -ENOENT;
	} else {
		return node->ops->create(node, name, namelen, args, childp);
	}
}

int vnode_unlink(vnode_t *node, const char *name, size_t namelen, int flags,
	vnode_t *child)
{
	int err, tmp = (flags & VNAMEI_DIR) ? AT_REMOVEDIR : 0;

	VN_ASSERT_LOCK_WR(node);
	assert(strcmp(name, ".."));
	assert(strcmp(name, "."));
	assert(node != child);

	wrlock_scope(&child->lock);
	err = node->ops->unlink(node, name, namelen, tmp, child);
	if(flags & VNAMEI_DIR) {
		if(!err) {
			assert(child->nlink == 0);
		}

		/*
		 * The directory only has the entries ".." and ".".
		 */
		vdirent_cache_try_rem(child, ".", 1);
		vdirent_cache_try_rem(child, "..", 2);
	}

	return err;
}

int vnode_rename(vnode_t *olddir, const char *oldname, size_t oldlen,
	vnode_t *old, vnode_t *newdir, const char *newname, size_t newlen,
	vnode_t *new)
{
	VN_ASSERT_LOCK_WR(olddir);
	VN_ASSERT_LOCK_WR(newdir);
	return olddir->ops->rename(olddir, oldname, oldlen, old,
		newdir, newname, newlen, new);
}

ssize_t vnode_readlink(vnode_t *node, char *buf, size_t size, vm_seg_t seg) {
	struct iovec iov;
	uio_t uio;

	if(!VN_ISLNK(node)) {
		return -EINVAL;
	}

	iov.iov_base = buf;
	iov.iov_len = size;
	uio_simple(&uio, &iov, 0, seg == KERNELSPACE ? UIO_KERN : UIO_USER,
		UIO_RD);

	wrlock_scope(&node->lock);
	return node->ops->readlink(node, &uio);
}

int vnode_chmod(vnode_t *node, mode_t mode) {
	uid_t uid;

	uid = proc_euid();
	if(uid != UID_ROOT && uid != node->uid) {
		return -EPERM;
	}

	wrlock(&node->lock);
	wrlock(&node->statlock);
	node->mode = (node->mode & ~CHMOD_BITS) | mode;
	rwunlock(&node->lock);
	vnode_settime(node, VN_CURTIME, VN_CTIME);
	vnode_dirty(node);
	rwunlock(&node->statlock);

	return vnode_sync(node);
}

int vnode_chown(vnode_t *node, uid_t uid, gid_t gid) {
	uid_t puid = proc_euid();

	/*
	 * A chown needs to be synchronized with read/write/namei etc,
	 * so the big vnode lock needs to be locked.
	 */
	wrlock(&node->lock);
	wrlock(&node->statlock);

	/*
	 * The owner of the file may change its group id.
	 */
	if(puid != UID_ROOT && (puid != node->uid ||
		(puid == node->uid && puid != uid)))
	{
		return -EPERM;
	}

	node->uid = uid;
	node->gid = gid;
	rwunlock(&node->lock);
	vnode_settime(node, VN_CURTIME, VN_CTIME);
	vnode_dirty(node);
	rwunlock(&node->statlock);

	return vnode_sync(node);
}

int vnode_utimes(vnode_t *node, struct timespec times[2]) {
	uid_t uid = proc_euid();
	struct timespec now;

	realtime(&now);
	wrlocked(&node->statlock) {
		if(uid != 0 && node->uid != uid) {
			if(times[0].tv_nsec != UTIME_NOW ||
				times[1].tv_nsec != UTIME_NOW)
			{
				return -EACCES;
			} else if(!vnode_access(node, VN_ACC_W)) {
				return -EACCES;
			}
		}

		if(times[0].tv_nsec == UTIME_NOW) {
			times[0] = now;
		}
		if(times[1].tv_nsec == UTIME_NOW) {
			times[1] = now;
		}

		vnode_dirty(node);
	}

	return vnode_sync(node);
}

int vnode_truncate(vnode_t *node, vnode_size_t size) {
	int err;

	wrlock_scope(&node->lock);
	if(node->size == size) {
		return 0;
	}

	err = node->ops->truncate(node, size);
	if(err == 0) {
		assert(node->size == size);
	}

	return err;
}

int vnode_stat(vnode_t *node, struct stat64 *stat) {
	stat->st_ino = node->ino;

	if(filesys_dev(node->fs)) {
		stat->st_dev = blk_provider_dev(filesys_dev(node->fs));
	} else {
		stat->st_dev = 0;
	}

	stat->st_rdev = node->rdev;
	stat->st_blksize = 1 << node->blksz_shift;

	rdlocked(&node->statlock) {
		stat->st_atim = node->atime;
		stat->st_mtim = node->mtime;
		stat->st_ctim = node->ctime;
		stat->st_mode = node->mode;
		stat->st_nlink = node->nlink;
		stat->st_uid = node->uid;
		stat->st_gid = node->gid;
		stat->st_blocks = node->blocks;
		stat->st_size = node->size;
	}

	return 0;
}

/**
 * @brief Call the write callback of a vnode.
 */
ssize_t vnode_write(vnode_t *node, uio_t *uio) {
	assert(uio->rw == UIO_WR);
	assert(VN_ISREG(node));
	VN_ASSERT_LOCK_WR(node);

	/*
	 * Don't try writing to an executable.
	 */
	if(vnode_is_exe(node)) {
		return -ETXTBSY;
	} else {
		return node->ops->write(node, uio);
	}
}

/**
 * @brief Call the read callback of a vnode.
 */
ssize_t vnode_read(vnode_t *node, uio_t *uio) {
	ssize_t out = 0;

	assert(uio->rw == UIO_RD);
	assert(VN_ISREG(node));
	VN_ASSERT_LOCK_RD(node);

	if((vnode_size_t)uio->off >= node->size) {
		uio->size = 0;
	} else {
		uio->size = min(uio->size, node->size - uio->off);
	}

	if(uio->size) {
		out = node->ops->read(node, uio);
	}

	return out;
}

int vnode_getpage(vnode_t *node, vm_objoff_t off, vm_flags_t flags,
	vm_page_t **pagep)
{
	vm_page_t *page;
	int err;

	err = vm_object_page_resident(VNTOVM(node), off, &page);
	if(!err && VM_PROT_WR_P(flags)) {
		/*
		 * Make sure that the page is considered dirty, so that
		 * pageout writes the contents to disk before freeing the
		 * page.
		 */
		vm_page_dirty(page);
	}

	*pagep = page;
	return err;
}

int vnode_bmap(vnode_t *node, bool alloc, blkno_t blk, blkno_t *pbn) {
	VN_ASSERT_LOCK_VM(node);
	return node->ops->bmap(node, alloc, blk, pbn);
}

static int vnode_pagein(vm_object_t *object, __unused vm_pghash_node_t *pgh,
	vm_page_t *page)
{
	vnode_t *node = VMTOVN(object);
	int err;

	VN_ASSERT_LOCK_VM(node);
	assert(pgh == NULL);

	/*
	 * Fill the page with the file data.
	 */
	err = node->ops->pagein(node, vm_page_offset(page), page);
	if(!err) {
		vm_page_unbusy(page);
	}

	return err;
}

static int vnode_pageout(vm_object_t *object, vm_page_t *page) {
	vnode_t *node = VMTOVN(object);

	VN_ASSERT_LOCK_VM(node);
	return node->ops->pageout(node, page);
}

static void vnode_vm_dirty(vm_object_t *object, vm_page_t *page) {
	vnode_t *node = VMTOVN(object);

	node->dirty++;

	/*
	 * This page should be synced in the very soon to prevent
	 * any massive data loss.
	 */
	vm_sync_needed(page, VM_SYNC_NORMAL);
}

static int vnode_vm_map(vm_object_t *object, vm_map_t *map) {
	vnode_t *node = VMTOVN(object);

	/*
	 * Somebody might have executed the vnode between the mmap file
	 * operation and the actual mapping. So check if we still can
	 * write-map the vnode. If the check succedes we add the mapping
	 * to the object while the vnode is still locked, thus elimenating
	 * the race condition between vnode_check_exe and memory mapping
	 * vnodes. Remember that this function is called during the actual
	 * mmap (i.e. the virtual address space is locked) and the mmap cannot
	 * fail once this function does not return an error.
	 */
	if(VM_PROT_WR_P(map->flags) && !VM_MAP_SHADOW_P(map->flags)) {
		wrlock_scope(&node->lock);
		if(vnode_is_exe(node)) {
			return -ETXTBSY;
		} else {
			vm_object_map_add(object, map);
		}
	} else {
		vm_object_map_add(object, map);
	}

	return 0;
}

static int vnode_vm_fault(vm_object_t *object, vm_objoff_t off,
	vm_flags_t access, __unused vm_flags_t *map_flags, vm_page_t **pagep)
{
	/*
	 * There are no data, which is not on disk, so the vm subsys requests
	 * every page from the vnode_pager rather than the fault callback of the
	 * object.
	 */
	(void) object;
	(void) off;
	(void) map_flags;
	(void) pagep;
	(void) access;
	kpanic("vnode_vm_fault");
}

static void vnode_vm_destroy(vm_object_t *object) {
	vnode_cache_zeroref(VMTOVN(object));
}

static int vnode_fop_open(file_t *file) {
	vnode_t *node = file_vnode(file);
	int access, err = 0;


	if(file->flags & O_TRUNC) {
		/*
		 * kern_openat checks for this.
		 */
		assert(FACC(file->flags) != O_RDONLY);

		/*
		 * No trunc for directories etc.
		 */
		if(file->type != FREG) {
			return -EINVAL;
		}
	}

	/*
	 * Calculate the needed permissions.
	 */
	access = 0;
	if(file->flags & O_PATH) {
		/*
		 * Remember that O_PATH is the same flag as O_EXEC and
		 * that VN_ACC_X means search permission for directories.
		 */
		access = VN_ACC_X;
	} else if(file->flags & O_NOFOLLOW && VN_ISLNK(node)) {
		/*
		 * Don't allow a symbolik link to be opened without O_PATH.
		 */
		return -ELOOP;
	}

	access |= file_readable(file) ? VN_ACC_R : 0;
	if(file_writeable(file) || (file->flags & O_TRUNC)) {
		access |= VN_ACC_W;
	}

	if((file->flags & O_TRUNC) || file_writeable(file)) {
		wrlock(&node->lock);
	} else {
		rdlock(&node->lock);
	}

	/*
	 * See if the current process has the appropriate
	 * permission.
	 */
	if(!vnode_access(node, access)) {
		err = -EACCES;
	} else if((access & VN_ACC_W) && vnode_is_exe(node)) {
		err = -ETXTBSY;
	} else if(file->flags & O_TRUNC) {
		assert(node->ops->truncate);
		err = node->ops->truncate(node, 0);
	}

	if(err == 0 && file_writeable(file)) {
		node->writecnt++;
	}

	rwunlock(&node->lock);
	return err;
}

static void vnode_fop_close(file_t *file) {
	vnode_t *node = file_vnode(file);

	if(file_writeable(file)) {
		vnode_lock(node, VNLOCK_EXCL);
		node->writecnt--;
		vnode_unlock(node);
	}
}

static ssize_t vnode_fop_rdwr(file_t *file, uio_t *uio) {
	vnode_t *node;
	ssize_t size;

	if(file->type != FREG) {
		return -EBADF;
	} else if(uio->size == 0) {
		return 0;
	}

	node = file_vnode(file);
	foff_lock_get_uio(file, uio);

	if(uio->rw == UIO_WR) {
		wrlock_scope(&node->lock);

		/*
		 * Set the offset to the end of the file, when appending data.
		 */
		if(file->flags & O_APPEND) {
			uio->off = node->size;
		}

		size = vnode_write(node, uio);
	} else {
		rdlock_scope(&node->lock);
		size = vnode_read(node, uio);
	}

	foff_unlock_uio(file, uio);

	return size;
}

static void vnode_fop_poll(__unused file_t *file, polldesc_t *desc) {
	static const int flags = POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM;

	if(desc->events & ~flags) {
		/* TODO */
		desc->revents = POLLNVAL;
	} else {
		desc->revents = desc->events & flags;
	}
}

static off_t vnode_fop_seek(file_t *file, off_t off, int whence) {
	vnode_t *node = file_vnode(file);
	off_t new = 0, old;
	int err = 0;

	assert(node);
	new = old = foff_lock_get(file);
	switch(whence) {
	case SEEK_SET:
		new = off;
		break;
	case SEEK_CUR:
		new = old + off;
		break;
	case SEEK_END:
		rdlocked(&node->statlock) {
			new = node->size + off;
		}
		break;
	default:
		err = -EINVAL;
	}

	if(new < 0) {
		err = -EINVAL;
	}

	/*
	 * Don't change the file offset if an error occured.
	 */
	if(err) {
		new = old;
	}

	foff_unlock(file, new);
	if(err) {
		return err;
	} else {
		return new;
	}
}

static int vnode_fop_stat(file_t *file, struct stat64 *stat) {
	return vnode_stat(file_vnode(file), stat);
}

static int vnode_fop_mmap(file_t *file, vm_objoff_t off, vm_vsize_t size,
	vm_flags_t *flagsp, vm_flags_t *max_prot, vm_object_t **out)
{
	const bool wrcheck = VM_MAP_SHARED_P(*flagsp) && VM_PROT_WR_P(*flagsp);
	vnode_t *node = file_vnode(file);
	vm_flags_t flags = *flagsp;
	vnode_size_t end;
	int access = 0;

	assert(node);
	if(!VN_ISREG(node) || (wrcheck && !file_writeable(file))) {
		return -EACCES;
	} else if(off > VN_SIZE_MAX || VN_SIZE_MAX - off < size) {
		return -EINVAL;
	}

	access |= VM_PROT_RD_P(flags) ? VN_ACC_R : 0;
	access |= VM_PROT_EXEC_P(flags) ? VN_ACC_X : 0;
	access |= wrcheck ? VN_ACC_W : 0;

	end = off + size;
	rdlocked(&node->lock) {
		if(end > ALIGN(node->size, PAGE_SZ)) {
			return -EINVAL;
		} else if(!vnode_access(node, access)) {
			return -EACCES;
		} else if(wrcheck && vnode_is_exe(node)) {
			/*
			 * We check if the node is an executable and deny write
			 * access if so. However after unlocking the node and
			 * before the actual mmap, one might execute the
			 * vnode. Thus we check for write permissions in the
			 * map callback of the object, which is called while
			 * mmaping (while the virtual address space is locked).
			 */
			return -ETXTBSY;
		}
	}

	*out = vm_object_ref(VNTOVM(node));
	if(VM_MAP_PRIV_P(flags)) {
		*flagsp |= VM_MAP_SHADOW;

		/*
		 * Private mappings can be granted write access eventhough
		 * the file was opened as read only.
		 */
		*max_prot |= VM_PROT_WR;
	}

	return 0;
}

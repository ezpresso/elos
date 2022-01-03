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
#include <kern/user.h>
#include <kern/proc.h>
#include <kern/signal.h>
#include <kern/futex.h>
#include <kern/time.h>
#include <vm/malloc.h>
#include <lib/string.h>
#include <vfs/vfs.h>
#include <vfs/sys.h>
#include <vfs/fs.h>
#include <vfs/file.h>
#include <vfs/proc.h>
#include <vfs/uio.h>
#include <vfs/vnode.h>
#include <vfs/vpath.h>
#include <vfs/lookup.h>
#include <sys/limits.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/dirent.h>

struct iovec;
struct dirent;
struct stat64;

static int getat(int at, const char *upath, int flags, vpath_t *res,
	char **path)
{
	file_t *start = NULL;
	int err;

	err = copyin_path(upath, path);
	if(err) {
		return err;
	}

	/*
	 * If the path is not relative _at_ is ignored.
	 */
	if((*path)[0] != '/' && at != AT_FDCWD) {
		start = fdget(at);
		if(start == NULL) {
			err = -EBADF;
		} else if(start->type != FDIR && !((flags & VNAMEI_EMPTY) &&
			!strcmp(*path, "")))
		{
			file_unref(start);
			err = -ENOTDIR;
		}

		if(err) {
			kfree(*path);
			return err;
		}
	}

	*res = VPATH_INIT;
	if(start) {
		vpath_cpy(res, file_vpath(start));
	}

	return 0;
}

static int user_lookupat(int at, const char *upath, int flags, vpath_t *out) {
	vpath_t start;
	char *path;
	int err;

	err = getat(at, upath, flags, &start, &path);
	if(err) {
		return err;
	}

	err = vlookup(&start, path, flags, out);
#if 0
	if(err) {
		kprintf("Failure %s\n", path);
	}
#endif

	kfree(path);
	vpath_clear(&start);

	return err;
}

#define FDGET_WR 	(1 << 0)
#define FDGET_RD 	(1 << 1)
#define FDGET_EROFS	(1 << 2)

/**
 * TODO move somewhere else
 */
static int fdget_acc(int fd, int acc, file_t **filep) {
	vpath_t *path;
	file_t *file;
	int err = 0;

	file = fdget(fd);
	if(file == NULL) {
		return -EBADF;
	}

	/*
	 * Check if the file can be accessed the way the caller requested.
	 */
	if((F_ISSET(acc, FDGET_WR) && !file_writeable(file)) ||
		(F_ISSET(acc, FDGET_RD) && !file_readable(file)))
	{
		err = -EBADF;
	} else if(F_ISSET(acc, FDGET_EROFS) && (path = file_vpath(file)) &&
		!vmount_writeable(path->mount))
	{
		err = -EROFS;
	}

	if(err) {
		file_unref(file);
		return err;
	} else {
		return *filep = file, 0;
	}
}

static int fd2node(int fd, int acc, vnode_t **nodep) {
	vnode_t *node;
	file_t *file;
	int err;

	err = fdget_acc(fd, acc, &file);
	if(err) {
		return err;
	}

	node = file_vnode(file);
	if(node) {
		*nodep = vnode_ref(node);
	} else {
		err = -EBADF;
	}

	file_unref(file);
	return err;
}

int sys_openat(int at, const char *upath, int flags, mode_t mode) {
	vpath_t start;
	file_t *file;
	char *path;
	int err;

	if(!(flags & O_LARGEFILE)) {
		return -ENOTSUP;
	}

	err = getat(at, upath, 0, &start, &path);
	if(err) {
		return err;
	}

	err = kern_openat(&start, path, flags, mode, &file);
	vpath_clear(&start);
	kfree(path);

	/*
	 * Allocate a fd for this file.
	 */
	if(err == 0) {
		err = fdalloc(file, !!F_ISSET(flags, O_CLOEXEC), 0);
		file_unref(file);
	}

#if 0
	if(err < 0) {
		kprintf("could not open %s\n", upath);
	} else {
		kprintf("open: success %s\n", upath);
	}
#endif

	return err;
}

int sys_open(const char *upath, int flags, mode_t mode) {
	return sys_openat(AT_FDCWD, upath, flags, mode);
}

int sys_close(int fd) {
	/*
	 * That's easy.
	 */
	return fdfree(fd);
}

int sys_fchownat(int dirfd, const char *upath, uid_t owner, gid_t group,
	int atflags)
{
	vpath_t path = VPATH_INIT;
	int err, flags;

	flags = VNAMEI_ANY;
	flags |= F_ISSET(atflags, AT_EMPTY_PATH) ? VNAMEI_EMPTY : 0;
	flags |= F_ISSET(atflags, AT_SYMLINK_NOFOLLOW) ? VNAMEI_NOFOLLOW : 0;

	err = user_lookupat(dirfd, upath, flags | VNAMEI_EROFS, &path);
	if(err) {
		return err;
	}

	err = vnode_chown(path.node, owner, group);
	vpath_clear(&path);
	return err;
}

int sys_chown32(const char *path, uid_t owner, gid_t group) {
	return sys_fchownat(AT_FDCWD, path, owner, group, 0);
}

int sys_lchown32(const char *path, uid_t owner, gid_t group) {
	return sys_fchownat(AT_FDCWD, path, owner, group, AT_SYMLINK_NOFOLLOW);
}

int sys_fchown32(int fd, uid_t owner, gid_t group) {
	vnode_t *node;
	int err;

	err = fd2node(fd, FDGET_EROFS, &node);
	if(err) {
		return err;
	}

	err = vnode_chown(node, owner, group);
	vnode_unref(node);

	return err;
}

int sys_fchmodat(int dirfd, const char *upath, mode_t mode, int aflags) {
	vpath_t link = VPATH_INIT;
	int err, flags;

	if(mode & ~CHMOD_BITS) {
		return -EINVAL;
	}

	flags = VNAMEI_ANY;
	flags |= F_ISSET(aflags, AT_SYMLINK_NOFOLLOW) ? VNAMEI_NOFOLLOW : 0;

	err = user_lookupat(dirfd, upath, flags | VNAMEI_EROFS, &link);
	if(err) {
		return err;
	}

	err = vnode_chmod(link.node, mode);
	vpath_clear(&link);
	return err;
}

int sys_chmod(const char *path, mode_t mode) {
	return sys_fchmodat(AT_FDCWD, path, mode, 0);
}

int sys_fchmod(int fd, mode_t mode) {
	vnode_t *node;
	int err;

	if(mode & ~CHMOD_BITS) {
		return -EINVAL;
	}

	err = fd2node(fd, FDGET_EROFS, &node);
	if(err) {
		return err;
	}

	err = vnode_chmod(node, mode);
	vnode_unref(node);
	return err;
}

int sys_mount(const char *usource, const char *utarget, const char *ufstype,
	unsigned long flags, __unused const void *data)
{
	char *source = NULL, *target, *type;
	int err;

	/*
	 * TODO
	 */
	(void) data;

	/*
	 * Copyin lots of strings...
	 */
	err = copyin_path(utarget, &target);
	if(err) {
		return err;
	}

	/*
	 * fstype is not a filesystem path, but this should work too.
	 */
	err = copyin_path(ufstype, &type);
	if(err) {
		goto err_type;
	}

	if(usource != NULL) {
		err = copyin_path(usource, &source);
	}

	if(err == 0) {
		err = kern_mount(source, target, type, flags, NULL);
	}

	if(source) {
		kfree(source);
	}

	kfree(type);
err_type:
	kfree(target);
	return err;
}

int sys_getcwd(char *ubuf, size_t size) {
	vpath_t cwd = VPATH_INIT;
	char *buf;
	int err;

	if(size < 2) {
		return -EINVAL;
	}

	size = MIN(size, PATH_MAX);
	buf = kmalloc(size, VM_NOFLAG);
	if(!buf) {
		return -ENOMEM;
	}

	vfs_get_cwd(&cwd);
	err = vfs_dirpath(&cwd, buf, size);
	vpath_clear(&cwd);
	if(!err) {
		err = copyout(ubuf, buf, size);
	}

	kfree(buf);

	return err;
}

int sys_chroot(const char *upath) {
	vpath_t vp = VPATH_INIT;
	int err;

	if(cur_proc()->euid != 0) {
		return -EPERM;
	}

	err = user_lookupat(AT_FDCWD, upath, VNAMEI_DIR, &vp);
	if(err) {
		return err;
	}

	vfs_set_root(&vp);
	vpath_clear(&vp);

	return 0;
}

int sys_chdir(const char *upath) {
	vpath_t vp = VPATH_INIT;
	int err;

	err = user_lookupat(AT_FDCWD, upath, VNAMEI_DIR, &vp);
	if(err) {
		return err;
	}

	vfs_set_cwd(&vp);
	vpath_clear(&vp);

	return 0;
}

int sys_fchdir(int fd) {
	file_t *file;
	int err = 0;

	file = fdget(fd);
	if(file == NULL) {
		return -EBADF;
	}

	if(file->type != FDIR) {
		err = -EBADF;
	} else {
		vfs_set_cwd(&file->path);
	}

	file_unref(file);
	return err;
}

static ssize_t sys_uio_rw(int fd, uio_t *uio) {
	file_t *file;
	ssize_t res;
	size_t size;

	file = fdget(fd);
	if(file == NULL) {
		return -EBADF;
	}

	/*
	 * Check if the file supports seeking and the file was opened
	 * for reading and/or writing.
	 */
	if((uio->flags & UIO_OFF) && file->fops->seek == NULL) {
		file_unref(file);
		return -EBADF;
	}

	size = uio->size;
	if(uio->rw == UIO_WR) {
		res = file_write(file, uio);
	} else {
		res = file_read(file, uio);
	}
	file_unref(file);

	/*
	 * If a read/write get's interrupted, some bytes may already
	 * have been read/written. Tell the caller the number of
	 * processed bytes rather than the interrupt error.
	 * Not returning -ERESTART if some bytes have already been
	 * read/written prevents interrrupted reads/writes from
	 * being restarted. A restarted read/write would have the
	 * same struct iovec as an argument and thus the bytes
	 * processed during the interrupted syscall would be discarded,
	 * which is obviously bogus.
	 * If however no data has been processed during the interrupted
	 * syscall, we can return -ERESTART.
	 */
	if((res == -EINTR || res == -ERESTART) && uio->size != size) {
		return size - uio->size;
	} else {
		return res;
	}
}

static ssize_t sys_prwv(int fd, struct iovec *iov, int iovc, off_t offset,
		uio_rw_t rw)
{
	ssize_t res;
	uio_t *uio;
	int err;

	if(iovc == 0) {
		return 0;
	}

	err = copyinuio(iov, iovc, &uio);
	if(err) {
		return err;
	}

	uio_init(uio, offset, UIO_USER, rw);
	res = sys_uio_rw(fd, uio);
	kfree(uio);

	return res;
}

static ssize_t sys_prw(int fd, void *buf, size_t len, off_t off, uio_rw_t rw) {
	struct iovec iov;
	uio_t uio;

	iov.iov_base = buf;
	iov.iov_len = len;
	uio_simple(&uio, &iov, off, UIO_USER, rw);

	return sys_uio_rw(fd, &uio);
}

ssize_t sys_preadv(int fd, struct iovec *iov, int iovc, SYSARG_LL(off)) {
	off_t offset = SYSCALL_LL(off);
	if(offset < 0) {
		return -EINVAL;
	}
	return sys_prwv(fd, iov, iovc, offset, UIO_RD);
}

ssize_t sys_pwritev(int fd, struct iovec *iov, int iovc, SYSARG_LL(off)) {
	off_t offset = SYSCALL_LL(off);
	if(offset < 0) {
		return -EINVAL;
	}

	return sys_prwv(fd, iov, iovc, offset, UIO_WR);
}

ssize_t sys_readv(int fd, struct iovec *iov, int iovc) {
	return sys_prwv(fd, iov, iovc, -1, UIO_RD);
}

ssize_t sys_writev(int fd, struct iovec *iov, int iovc) {
	return sys_prwv(fd, iov, iovc, -1, UIO_WR);
}

ssize_t sys_pread64(int fd, void *buf, size_t len, SYSARG_LL(off)) {
	off_t offset = SYSCALL_LL(off);
	if(offset < 0) {
		return -EINVAL;
	}
	return sys_prw(fd, buf, len, offset, UIO_RD);
}

ssize_t sys_pwrite64(int fd, void *buf, size_t len, SYSARG_LL(off)) {
	off_t offset = SYSCALL_LL(off);
	if(offset < 0) {
		return -EINVAL;
	}
	return sys_prw(fd, buf, len, offset, UIO_WR);
}

ssize_t sys_read(int fd, void *buf, size_t len) {
	return sys_prw(fd, buf, len, -1, UIO_RD);
}

ssize_t sys_write(int fd, void *buf, size_t len) {
	return sys_prw(fd, buf, len, -1, UIO_WR);
}

int sys_ioctl(int fd, int request, void *argp) {
	file_t *file;
	int err;

	file = fdget(fd);
	if(file == NULL) {
		return -EBADF;
	}

	err = file_ioctl(file, request, argp);
	file_unref(file);

	return err;
}

int sys_getdents64(int fd, struct dirent *dirp, size_t count) {
	struct iovec iov;
	file_t *file;
	uio_t uio;
	off_t off;
	int res;

	if(count < sizeof(*dirp)) {
		return -EINVAL;
	}

	file = fdget(fd);
	if(file == NULL) {
		return -EINVAL;
	} else if(file->type != FDIR) {
		file_unref(file);
		return -ENOTDIR;
	}

	iov.iov_len = count;
	iov.iov_base = dirp;

	off = foff_lock_get(file);
	uio_simple(&uio, &iov, off, UIO_USER, UIO_RD);
	res = vnode_getdents(file->path.node, &uio);
	foff_unlock(file, uio.off);
	file_unref(file);

	return res;
}

#define SETFL_FLAGS (O_APPEND | O_NOATIME | O_NONBLOCK)

int sys_fcntl64(int fd, int cmd, int arg) {
	bool cloexec = false;
	int res, flags, old;
	file_t *file = NULL;

	if(cmd != F_GETFD && cmd != F_SETFD) {
		file = fdget(fd);
		if(!file) {
			return -EBADF;
		}
	}

	switch(cmd) {
	case F_DUPFD_CLOEXEC:
		cloexec = true;
		/* FALLTHROUGH */
	case F_DUPFD:
		res = fdalloc(file, cloexec, arg);
		break;
	case F_GETFL:
		res = file->flags;
		break;
	case F_SETFL:
		if(arg & ~SETFL_FLAGS) {
			res = -EINVAL;
		} else do {
			/*
			 * TODO
			 */
			old = flags = atomic_load(&file->flags);
			flags = (flags & ~SETFL_FLAGS) | arg;
		} while(!atomic_cmpxchg(&file->flags, old, flags));

		res = 0;
		break;
	case F_GETFD:
		return fd_cloexec_get(fd);
	case F_SETFD:
		return fd_cloexec_set(fd, !!(arg & O_CLOEXEC));
	default:
		res = -EINVAL;
	}

	file_unref(file);

	return res;
}

int sys_fstatat64(int dirfd, const char *upath, struct stat64 *buf,
	int atflags)
{
	vpath_t path = VPATH_INIT;
	int err, flags = 0;
	struct stat64 tmp;

	if(atflags & AT_NO_AUTOMOUNT) {
		return -ENOTSUP;
	}

	if(atflags & AT_SYMLINK_NOFOLLOW) {
		flags |= VNAMEI_NOFOLLOW;
	}

	err = user_lookupat(dirfd, upath, flags | VNAMEI_ANY, &path);
	if(err == 0) {
		err = vnode_stat(path.node, &tmp);
		vpath_clear(&path);

		if(err == 0) {
			err = copyout(buf, &tmp, sizeof(tmp));
		}
	}

	return err;
}

int sys_stat64(const char *upath, struct stat64 *ubuf) {
	return sys_fstatat64(AT_FDCWD, upath, ubuf, 0);
}

int sys_lstat64(const char *upath, struct stat64 *ubuf) {
	return sys_fstatat64(AT_FDCWD, upath, ubuf, AT_SYMLINK_NOFOLLOW);
}

int sys_fstat64(int fd, struct stat64 *ubuf) {
	struct stat64 tmp;
	file_t *file;
	int err;

	file = fdget(fd);
	if(file == NULL) {
		return -EBADF;
	}

	err = file_stat(file, &tmp);
	file_unref(file);

	if(!err) {
		err = copyout(ubuf, &tmp, sizeof(tmp));
	}

	return err;
}

int sys_statfs64(const char *upath, struct statfs *buf) {
	vpath_t path = VPATH_INIT;
	struct statfs statfs;
	int err;

	err = user_lookupat(AT_FDCWD, upath, VNAMEI_ANY, &path);
	if(err) {
		return err;
	}

	memset(&statfs.f_spare, 0, sizeof(statfs.f_spare));
	filesys_stat(path.node->fs, &statfs);
	vpath_clear(&path);

	return copyout(buf, &statfs, sizeof(statfs));
}

int sys_fstatfs64(int fd, struct statfs *buf) {
	struct statfs statfs;
	vnode_t *node;
	int err;

	err = fd2node(fd, 0, &node);
	if(err) {
		return err;
	}

	memset(&statfs.f_spare, 0, sizeof(statfs.f_spare));
	filesys_stat(node->fs, &statfs);
	vnode_unref(node);
	return copyout(buf, &statfs, sizeof(statfs));
}

static bool utimens_check(struct timespec *ts) {
	if(ts->tv_nsec != UTIME_NOW && ts->tv_nsec != UTIME_OMIT &&
		ts->tv_nsec >= SEC_NANOSECS)
	{
		return false;
	} else {
		return true;
	}
}

int sys_utimensat(int fd, const char *pathname,
	const struct timespec *utimes, int flags)
{
	struct timespec times[2];
	vnode_t *node;
	int err;

	if(flags & ~AT_SYMLINK_NOFOLLOW) {
		return -EINVAL;
	} else if(utimes == NULL) {
		return -EACCES;
	} else if(flags & AT_SYMLINK_NOFOLLOW) {
		flags = VNAMEI_NOFOLLOW;
	}

	if(utimes) {
		err = copyin(times, utimes, sizeof(times));
		if(err) {
			return err;
		}

		if(!utimens_check(&times[0]) || !utimens_check(&times[1])) {
			return -EINVAL;
		}

		if(times[0].tv_nsec == UTIME_OMIT &&
			times[1].tv_nsec == UTIME_OMIT)
		{
			return 0;
		}
	} else {
		times[0].tv_nsec = UTIME_NOW;
		times[0].tv_sec = 0;
		times[1] = times[0];
	}

	/*
	 * Mimic linux's behaviour.
	 */
	if(pathname == NULL) {
		err = fd2node(fd, FDGET_EROFS, &node);
		if(err) {
			return err;
		}
	} else {
		vpath_t vp = VPATH_INIT;

		err = user_lookupat(fd, pathname, flags | VNAMEI_EROFS, &vp);
		if(err) {
			return err;
		}

		node = vnode_ref(vp.node);
		vpath_clear(&vp);
	}

	err = vnode_utimes(node, times);
	vnode_unref(node);

	return err;
}

int sys_faccessat(int dirfd, const char *upath, int mode, int aflags) {
	vpath_t path = VPATH_INIT;
	int err, flags = 0, lkup_flags;

	if(mode & ~(R_OK | W_OK | X_OK)) {
		return -EINVAL;
	}

	if(mode & R_OK) {
		flags |= VN_ACC_R;
	}
	if(mode & W_OK) {
		flags |= VN_ACC_W;
	}
	if(mode & X_OK) {
		flags |= VN_ACC_X;
	}
	if((aflags & AT_EACCESS) == 0) {
		flags |= VN_ACC_REAL;
	}

	lkup_flags = VNAMEI_ANY;
	if(aflags & AT_SYMLINK_NOFOLLOW) {
		lkup_flags = VNAMEI_NOFOLLOW;
	}

	err = user_lookupat(dirfd, upath, lkup_flags, &path);
	if(err) {
		return err;
	}

	if(mode != F_OK) {
		if(mode & W_OK && !vmount_writeable(path.mount)) {
			err = -EROFS;
		} else {
			vnode_lock(path.node, VNLOCK_SHARED);
			if(vnode_access(path.node, flags) == false) {
				err = -EACCES;
			}
			vnode_unlock(path.node);
		}
	}

	vpath_clear(&path);
	return err;
}

int sys_access(const char *upath, int mode) {
	return sys_faccessat(AT_FDCWD, upath, mode, 0);
}

int sys_renameat(int olddirfd, const char *u_oldpath, int newdirfd,
	const char *u_newpath)
{
	vpath_t old_at, new_at;
	char *oldpath, *newpath;
	int err;

	err = getat(olddirfd, u_oldpath, 0, &old_at, &oldpath);
	if(err) {
		return err;
	}

	err = getat(newdirfd, u_newpath, 0, &new_at, &newpath);
	if(err == 0) {
		err = kern_renameat(&old_at, oldpath, &new_at, newpath);
		vpath_clear(&new_at);
		kfree(newpath);
	}

	vpath_clear(&old_at);
	kfree(oldpath);
	return err;
}

int sys_rename(const char *oldpath, const char *newpath) {
	return sys_renameat(AT_FDCWD, oldpath, AT_FDCWD, newpath);
}

#if 0
int sys_linkat(int olddirfd, const char *oldpath, int newdirfd,
		const char *newpath, int flags)
{
	vpath_t old = VPATH_INIT;
	int flags, err;

#if 0
AT_EMPTY_PATH
AT_SYMLINK_FOLLOW
#endif
}

int sys_link(const char *oldpath, const char *newpath) {
	return sys_linkat(AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
}
#endif

int sys_truncate64(const char *upath, off_t length) {
	vpath_t path = VPATH_INIT;
	int err;

	if(length < 0) {
		return -EINVAL;
	}

	err = user_lookupat(AT_FDCWD, upath, VNAMEI_EROFS, &path);
	if(err == 0) {
		err = vnode_truncate(path.node, length);
		vpath_clear(&path);
	}

	return err;
}

int sys_ftruncate64(int fd, off_t length) {
	vnode_t *node;
	int err;

	if(length < 0) {
		return -EINVAL;
	}

	err = fd2node(fd, FDGET_WR | FDGET_EROFS, &node);
	if(err) {
		return err;
	}

	err = vnode_truncate(node, length);
	vnode_unref(node);

	return err;
}

int sys_unlinkat(int dirfd, const char *upath, int flags) {
	vpath_t start;
	char *path;
	int err;

	if(flags & ~AT_REMOVEDIR) {
		return -EINVAL;
	} else if(flags & AT_REMOVEDIR) {
		flags = VNAMEI_DIR;
	}

	err = getat(dirfd, upath, 0, &start, &path);
	if(err) {
		return err;
	}

	err = kern_unlinkat(&start, path, flags);
	vpath_clear(&start);
	kfree(path);

	return err;
}

int sys_unlink(const char *upath) {
	return sys_unlinkat(AT_FDCWD, upath, 0);
}

int sys_rmdir(const char *upath) {
	return sys_unlinkat(AT_FDCWD, upath, AT_REMOVEDIR);
}

int sys_symlinkat(const char *utarget, int newdirfd, const char *linkpath) {
	vpath_t at = VPATH_INIT;
	char *path, *target;
	int err;

	err = copyin_path(utarget, &target);
	if(err) {
		return err;
	}

	err = getat(newdirfd, linkpath, 0, &at, &path);
	if(err == 0) {
		err = kern_symlinkat(target, &at, path);
		vpath_clear(&at);
		kfree(path);
	}

	kfree(target);
	return err;
}

int sys_symlink(const char *target, __unused const char *linkpath) {
	return sys_symlinkat(target, AT_FDCWD, linkpath);
}

ssize_t sys_readlinkat(int dirfd, const char *upath, char *buf, size_t size) {
	vpath_t link = VPATH_INIT;
	ssize_t res;

	res = user_lookupat(dirfd, upath, VNAMEI_NOFOLLOW, &link);
	if(res == 0) {
		res = vnode_readlink(link.node, buf, size, USERSPACE);
		vpath_clear(&link);
	}

	return res;
}

ssize_t sys_readlink(const char *upath, char *buf, size_t size) {
	return sys_readlinkat(AT_FDCWD, upath, buf, size);
}

/*
 * Eventough dev_t is 64 bits wide, an int is used here. Otherwise
 * this does not work (on i386).
 */
int sys_mknodat(int at, const char *upath, mode_t mode, unsigned dev) {
	vpath_t start;
	char *path;
	int err;

	err = getat(at, upath, 0, &start, &path);
	if(err) {
		return err;
	}

	err = kern_mknodat(&start, path, VFS_APPLY_UMASK(mode), dev);
	vpath_clear(&start);
	kfree(path);

	return err;
}

int sys_mknod(const char *upath, mode_t mode, unsigned dev) {
	return sys_mknodat(AT_FDCWD, upath, mode, dev);
}

int sys_mkdirat(int at, const char *upath, mode_t mode) {
	vpath_t start;
	char *path;
	int err;

	if(mode > 0777) {
		return -EINVAL;
	}

	err = getat(at, upath, 0, &start, &path);
	if(err) {
		return err;
	}

	err = kern_mkdirat(&start, path, VFS_APPLY_UMASK(mode));
	vpath_clear(&start);
	kfree(path);

	return err;
}

int sys_mkdir(const char *path, mode_t mode) {
	return sys_mkdirat(AT_FDCWD, path, mode);
}

int sys_dup(int fd) {
	file_t *file;
	int err;

	file = fdget(fd);
	if(file == NULL) {
		return -EBADF;
	}

	err = fdalloc(file, false, 0);
	file_unref(file);
	return err;
}

int sys_dup2(int oldfd, int newfd) {
	file_t *file;
	int err;

	file = fdget(oldfd);
	if(file == NULL) {
		return -EBADF;
	}

	err = fddup(file, newfd);
	file_unref(file);
	return err;
}

int sys__llseek(int fd, unsigned long off_hi, unsigned long off_lo,
	loff_t *result, int whence)
{
	off_t off = ((uint64_t)off_hi << 32) | off_lo;
	file_t *file;
	int err;

	file = fdget(fd);
	if(file == NULL) {
		return -EBADF;
	}

	err = off = file_seek(file, off, whence);
	file_unref(file);

	if(off >= 0) {
		err = copyout(result, &off, sizeof(off));
	}

	return err;
}

mode_t sys_umask(mode_t mask) {
	return vfs_set_umask(mask);
}

int sys_fadvise64_64(__unused int fd, SYSARG_LL(off), SYSARG_LL(len),
	__unused int advice)
{
	(void) SYSCALL_LL(off);
	(void) SYSCALL_LL(len);
	return -ENOSYS;
}

/*
 * Select is completely broken -- I don't want to implement it...
 */
int sys_pselect6(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	const struct timespec *timeout, const sigset_t *sigmask)
{
	(void) nfds;
	(void) readfds;
	(void) writefds;
	(void) exceptfds;
	(void) timeout;
	(void) sigmask;

	return 0;// -ENOSYS;
}

int sys__newselect(int nfds, fd_set *readfds, fd_set *writefds,
	fd_set *exceptfds, const struct timespec *timeout)
{
	(void) nfds;
	(void) readfds;
	(void) writefds;
	(void) exceptfds;
	(void) timeout;

	return -ENOSYS;
}

#if 0
void file_event(file_t *file, int events) {
	pollwait_t *wait;

	sync_scope_acquire(&file->pollq_lock);
	foreach(wait, &file->pollq) {
		if(wait->events & events) {
			list_remove(&file->pollq, &wait->node);
			wait->finished = 1;
			sched_wakeup(wait->thread);
		}
	}
}

void file_poll_wait(file_t *file, int events) {
	pollwait_t *wait;

	wait = pollwait_new();
	wait->events = events;
	wait->threads = threads;
	wait->file = file_ref(file);

	synchronized(&file->pollq_lock) {
		list_append(&file->pollq, &wait->node);
	}
}

void poll_stop(thread_t *thread) {
	foreach(cur, &thread->poll) {
		synchronized(&cur->file->pollq_lock) {
			if(!cur->finished) {
				remove;
			}
		}
	}
}

int pipe_poll(file_t *file, int events) {
	events &= POLLIN | POLLOUT | POLLHUP;
	file_poll(file, events);

}
#endif

static void pollinit(poll_t *poll, polldesc_t *desc, nfds_t nfds) {
	poll->fds = desc;
	poll->nfds = nfds;
	poll->done = false;

	for(nfds_t i = 0; i < nfds; i++) {
		list_node_init(&desc[i], &desc[i].node);
		desc[i].poll = poll;
		desc[i].done = false;
	}
}

static void polluninit(poll_t *poll) {
	for(nfds_t i = 0; i < poll->nfds; i++) {
		list_node_destroy(&poll->fds[i].node);
	}
}

static nfds_t pollscan(poll_t *poll, nfds_t *waitingp) {
	nfds_t finished = 0, waiting = 0;

	for(nfds_t i = 0; i < poll->nfds; i++) {
		polldesc_t *desc = &poll->fds[i];
		file_t *file;

		if(desc->fd < 0) {
			continue;
		}

		assert(desc->file == NULL);
		assert(desc->done == 0);

		file = fdget(desc->fd);
		if(file == NULL) {
			desc->revents = POLLNVAL;
			finished++;
			continue;
		}

		file_poll(file, desc);
		if(desc->events) {
			finished++;
		} else {
			waiting++;
		}
	}

	*waitingp = 0;
	return finished;
}

static void pollstop(poll_t *poll) {
	for(nfds_t i = 0; i < poll->nfds; i++) {
		polldesc_t *desc = &poll->fds[i];

		if(desc->file) {
			synchronized(&desc->file->pollq_lock) {
				if(desc->done == false) {
					list_remove(&desc->file->pollq,
						&desc->node);
				}
			}

			file_unref(desc->file);
			desc->file = NULL;
			desc->done = false;
		}
	}
}

int kern_poll(struct pollfd *fds, nfds_t nfds, struct timespec *timeout,
	sigset_t *mask)
{
	struct pollfd pollfd;
	int err, retv = 0;
	nfds_t waiting, i;
	polldesc_t *desc;
	poll_t poll;
	sigset_t old;

	/*
	 * The poll callback is implemented nowhere and
	 * it wasn't testet.
	 */
	kpanic("POLL");

	/* TODO */
	(void) timeout;

	if(nfds > PROC_FILES) {
		return -EINVAL;
	}

	desc = kmalloc(nfds * sizeof(polldesc_t), VM_NOFLAG);
	if(desc == NULL) {
		return -ENOMEM;
	}

	for(i = 0; i < nfds; i++) {
		err = copyin(&pollfd, &fds[i], sizeof(struct pollfd));
		if(err) {
			goto err_fds;
		}

		desc[i].fd = pollfd.fd;
		desc[i].events = pollfd.events;
		desc[i].revents = pollfd.revents;
	}

	if(mask) {
		/*
		 * See https://lwn.net/Articles/176911/ for reason.
		 */
		err = kern_sigprocmask(SIG_SETMASK, mask, &old);
		assert(!err);
	}

	/*
	 * Start polling.
	 */
	pollinit(&poll, desc, nfds);
	do {
		atomic_store_relaxed(&poll.done, false);
		retv = pollscan(&poll, &waiting);
		if(retv == 0 && waiting) {
			retv = kern_wait(&poll.done, false, KWAIT_INTR);
		}

		pollstop(&poll);
	} while(retv == 0 && waiting != 0);
	polluninit(&poll);
	kfree(poll.fds);

	/*
	 * Restore old mask.
	 */
	err = kern_sigprocmask(SIG_SETMASK, mask, &old);
	assert(!err);

	return retv;

err_fds:
	kfree(desc);
	return err;
}

int sys_poll(struct pollfd *fds, nfds_t nfds, int timeout) {
	struct timespec ts, *tsp;

	if(timeout < 0) {
		tsp = NULL;
	} else {
		/*
		 * The timeout is in milliseconds.
		 */
		ts.tv_sec = timeout / 1000;
		ts.tv_nsec = MILLI2NANO(timeout % 1000);
		tsp = &ts;
	}

	return kern_poll(fds, nfds, tsp, NULL);
}

int sys_ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout,
	const sigset_t *umask, size_t sigsetsz)
{
	struct timespec ts, *tsp = NULL;
	sigset_t mask, *maskp = NULL;
	int err;

	if(sigsetsz != sizeof(sigset_t)) {
		return -EINVAL;
	}

	if(umask != NULL) {
		err = copyin(&mask, umask, sizeof(mask));
		if(err) {
			return err;
		}

		maskp = &mask;
	}

	if(timeout) {
		err = copyin_ts(&ts, timeout);
		if(err) {
			return err;
		}

		tsp = &ts;
	}

	return kern_poll(fds, nfds, tsp, maskp);
}

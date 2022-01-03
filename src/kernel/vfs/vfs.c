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
#include <kern/init.h>
#include <kern/proc.h>
#include <kern/sync.h>
#include <vfs/vfs.h>
#include <vfs/vpath.h>
#include <vfs/vcache.h>
#include <vfs/lookup.h>
#include <vfs/fs.h>
#include <vfs/vmount.h>
#include <vfs/file.h>
#include <vfs/proc.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/limits.h>
#include <lib/string.h>

static sync_t vfs_mountlock = SYNC_INIT(MUTEX);
vpath_t vfs_root = VPATH_INIT;

#define SUPPORTED (O_DIRECTORY | O_ACCMODE | O_CREAT | O_EXCL | O_LARGEFILE | \
	   O_NOCTTY | O_CLOEXEC | O_APPEND | O_TRUNC | O_NONBLOCK | \
	   O_NOFOLLOW)

int kern_create(vpath_t *at, const char *path, int flags, mode_t mode,
	vpath_t *result, ... /* dev_t dev / const char *symlink */)
{
	vnode_create_args_t args;
	vnamei_t namei;
	va_list va;
	int err;

	/*
	 * Make sure that the caller initialized the result vpath to zero.
	 */
	if(result) {
		ASSERT_VPATH_NULL(result);
	}

	if(at && at->node) {
		assert(VN_ISDIR(at->node));
		assert(path[0] != '/');
	}

	if(!S_ICHECK(mode & ~S_IFMT)) {
		return -EINVAL;
	}

	/*
	 * Symlinks and device special files need further information.
	 */
	va_start(va, result);
	if(S_ISLNK(mode)) {
		args.symlink.name = va_arg(va, const char *);
		args.symlink.len = strlen(args.symlink.name);
	} else {
		args.dev = va_arg(va, dev_t);
	}
	va_end(va);

	args.mode = VFS_APPLY_UMASK(mode);
	proc_get_eids(&args.uid, &args.gid);

	/*
	 * Lookup the parent vnode and look whether the child vnode
	 * already exists.
	 */
	err = vnamei(at, path, flags |  VNAMEI_LOCKPARENT | VNAMEI_OPTIONAL |
		(S_ISDIR(mode) ? VNAMEI_DIR : 0), VNAMEI_CREATE, &namei);
	if(err) {
		return err;
	}

	if(vpath_is_empty(&namei.path)) {
		vnode_t *child;

		/*
		 * A new node has to be created in the parent directory,
		 * which is obviously not possible in a read-only filesystem.
		 */
		if(!vmount_writeable(namei.parent.mount)) {
			err = -EROFS;
			goto out;
		}

		/*
		 * Create the child node.
		 */
		err = vnode_create(namei.parent.node, namei.childname,
			namei.namelen, &args, &child);
		if(err) {
			goto out;
		}

		if(result) {
			result->node = child;
			result->mount =	vmount_ref(namei.parent.mount);
		} else {
			vnode_unref(child);
		}
	} else if(result) {
		assert(!(flags & VNAMEI_EXCL));
		vpath_cpy(result, &namei.path);
	}

out:
	vnamei_done_unlock(&namei);
	return err;
}

int kern_mkdirat(vpath_t *at, const char *path, mode_t mode) {
	return kern_create(at, path, VNAMEI_EXCL, mode | S_IFDIR, NULL);
}

int kern_mknodat(vpath_t *at, const char *path, mode_t mode, dev_t dev) {
	if(!S_ISREG(mode) && !S_ISCHR(mode) && !S_ISBLK(mode) &&
		!S_ISFIFO(mode) && !S_ISSOCK(mode))
	{
		return -EINVAL;
	}

	return kern_create(at, path, VNAMEI_EXCL, mode, NULL, dev);
}

int kern_symlinkat(const char *target, vpath_t *at, const char *linkpath) {
	/*
	 * The permissions of symbolic links do not really matter.
	 */
	return kern_create(at, linkpath, VNAMEI_EXCL, 0777 | S_IFLNK, NULL,
		target);
}

int kern_openat(vpath_t *at, const char *path, int flags, mode_t mode,
	file_t **result)
{
	vpath_t res_path = VPATH_INIT;
	int err, nflags = 0;
	file_t *file;

	if(at && at->node) {
		assert(VN_ISDIR(at->node));
		assert(path[0] != '/');
	}

	if(flags & ~SUPPORTED) {
		kprintf("[open] warning: unsupported flags: %d",
			flags & ~SUPPORTED);
		return -EINVAL;
	}

	/*
	 * If these 3 lines are ever removed, remember to check for correct
	 * permissions in vnode_open.
	 */
	if(FACC(flags) == O_RDONLY && F_ISSET(flags, O_TRUNC)) {
		return -EINVAL;
	}

	if(F_ISSET(flags, O_PATH)) {
		flags &= (O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
	}

	nflags |= F_ISSET(flags, O_NOFOLLOW) ? VNAMEI_NOFOLLOW : 0;
	if(F_ISSET(flags, O_CREAT)) {
		/*
		 * Sanity check the mode. Furthermore a directory cannot be
		 * created using open.
		 */
		if(!S_ICHECK(mode) || (flags & O_DIRECTORY)) {
			return -EINVAL;
		} else if(F_ISSET(flags, O_EXCL)) {
			nflags |= VNAMEI_EXCL;
		}

		err = kern_create(at, path, nflags, mode | S_IFREG, &res_path);
	} else {
		if(F_ISSET(flags, O_EXCL)) {
			return -EINVAL;
		}

		nflags |= F_ISSET(flags, O_DIRECTORY) ? VNAMEI_DIR : VNAMEI_ANY;
		err = vlookup(at, path, nflags, &res_path);
	}

#if 0
	if(err) {
		kprintf("openerr: %s\n", path);
	}
#endif

	if(err) {
		return err;
	}

	if(FWRITEABLE(flags)) {
		if(!vmount_writeable(res_path.mount)) {
			err = -EROFS;
		} else if(VN_ISDIR(res_path.node)) {
			err = -EISDIR;
		}

		if(err) {
			vpath_clear(&res_path);
			return err;
		}
	}

	/*
	 * Now that the vnode is found a file structure is needed.
	 */
	err = file_alloc_path(&res_path, flags, &file);
	vpath_clear(&res_path);
	if(err) {
		return err;
	}

	err = file_open(file);
	if(err) {
		file_free(file);
	} else {
		*result = file;
	}

	return err;
}

int kern_open(const char *path, int flags, mode_t mode, file_t **result) {
	return kern_openat(NULL, path, flags, mode, result);
}

void kern_close(file_t *file) {
	file_unref(file);
}

int kern_unlinkat(vpath_t *at, const char *path, int flags) {
	vnamei_t namei;
	int err;

	flags |= VNAMEI_NOFOLLOW | VNAMEI_LOCKPARENT | VNAMEI_EROFS;
	err = vnamei(at, path, flags, VNAMEI_UNLINK, &namei);
	if(err) {
		return err;
	}

	/*
	 * TODO make sure that namei.path.node is not a mount-point if the
	 * VNAMEI_DIR flag  is set.
	 */
	err = vnode_unlink(namei.parent.node, namei.childname, namei.namelen,
		flags, namei.path.node);
	if(err == 0) {
		/*
		 * Remove the old cached directory entry.
		 */
		if(namei.dirent) {
			vdirent_cache_rem(namei.parent.node, namei.dirent);

			/*
			 * Make sure that vnamei_done_unlock does not call
			 * vdirent_cache_put().
			 */
			namei.dirent = NULL;
		}
	}

	vnamei_done_unlock(&namei);

	return err;
}

static void vfs_rename_lock(vnode_t *dir1, vnode_t *dir2) {
	vnode_t *first, *scnd;

	/*
	 * Don't need to lock twice in case they are the same.
	 */
	if(dir1 == dir2) {
		vnode_lock(dir1, VNLOCK_EXCL);
		return;
	}

	/*
	 * Locking 2 vnodes => order is important here to avoid a deadlock.
	 * There are no other places where two directories are locked
	 * simultaniously (except mkdir, but that's a different story, since
	 * one vnode is freshly allocated there), so I think it's sufficient to
	 * just lock the vnode with the smaller inode-number first.
	 */
	if(dir1->ino < dir2->ino) {
		first = dir1;
		scnd = dir2;
	} else {
		first = dir2;
		scnd = dir1;
	}

	vnode_lock(first, VNLOCK_EXCL);
	vnode_lock(scnd, VNLOCK_EXCL);
}

/*
 * When renaming a directory, the target directory must not be inside
 * the source directory.
 */
static int vfs_rename_dircheck(vnode_t *sparent, vnode_t *sdir,
	vnode_t *tparent)
{
	vnode_t *node;
	int err = 0;

	/*
	 * TODO use cache
	 * TODO possible deadlock?
	 */

	node = vnode_ref(tparent);
	while(node->ino != node->fs->root) {
		vnode_t *parent;
		ino_t ino;

		assert(node != sdir);
		if(node != tparent && node != sparent) {
			vnode_lock(node, VNLOCK_EXCL);
		}

		err = vnode_namei(node, "..", 2, VNAMEI_LOOKUP, &ino);
		if(node != tparent && node != sparent) {
			vnode_unlock(node);
		}

		if(err) {
			assert(err != -ENOENT);
			goto out;
		}

		if(ino == sdir->ino) {
			err = -EINVAL;
			goto out;
		}

		err = filesys_vget(node->fs, ino, &parent);
		if(err) {
			goto out;
		}

		vnode_unref(node);
		node = parent;
	}

out:
	vnode_unref(node);
	return err;
}

int kern_renameat(vpath_t *oldat, const char *old, vpath_t *newat,
	const char *new)
{
	vnamei_t onamei, nnamei;
	vnode_t *vold, *vnew = NULL;
	ino_t ino;
	int err;

	/*
	 * Lookup the parent of the old path.
	 */
	err = vnamei(oldat, old, VNAMEI_WANTPARENT | VNAMEI_EROFS,
		VNAMEI_LOOKUP, &onamei);
	if(err) {
		return err;
	}

	/*
	 * Lookup the parent of the new path.
	 */
	err = vnamei(newat, new, VNAMEI_WANTPARENT | VNAMEI_EROFS,
		VNAMEI_LOOKUP, &nnamei);
	if(err) {
		goto err_namei_new;
	}

	/*
	 * Both directories have to be on the same filesystem.
	 */
	if(onamei.parent.node->fs != nnamei.parent.node->fs) {
		err = -EXDEV;
		goto err_fs;
	}

	/*
	 * Lock both directories.
	 */
	vfs_rename_lock(onamei.parent.node, nnamei.parent.node);

	/*
	 * Check for permissions.
	 * TODO what about the file being deleted?
	 */
	if(!vnode_access(onamei.parent.node, VN_ACC_W) ||
		!vnode_access(nnamei.parent.node, VN_ACC_W))
	{
		err = -EPERM;
		goto err_perm;
	}

	/*
	 * Lookup the old inode (don't care about cache since the finddir
	 * callback may save some important values during the finddir
	 * callback, needed for the subsequent rename callback)
	 */
	err = vnode_namei(onamei.parent.node, onamei.childname,
		onamei.namelen, VNAMEI_UNLINK, &ino);
	if(err) {
		goto err_find_old;
	}

	/*
	 * Get the vnode of the old inode.
	 */
	err = filesys_vget(onamei.parent.node->fs, ino, &vold);
	if(err) {
		goto err_find_old;
	}

	/*
	 * Renaming a directory requires some further attention.
	 */
	if(VN_ISDIR(vold) && onamei.parent.node != nnamei.parent.node) {
		err = vfs_rename_dircheck(onamei.parent.node, vold,
			nnamei.parent.node);
		if(err) {
			goto err_dircheck;
		}
	}

	/*
	 * Lookup the new vnode (it may or may not be present).
	 */
	err = vnode_namei(nnamei.parent.node, nnamei.childname, nnamei.namelen,
		VNAMEI_RENAME, &ino);
	if(err == -ENOENT) {
		vnew = NULL;
		err = 0;
	} else if(!err) {
		err = filesys_vget(nnamei.parent.node->fs, ino, &vnew);
	}

	if(err) {
		goto err_dircheck;
	}

	if(vnew && vnew != vold) {
		if(VN_ISDIR(vold) && !VN_ISDIR(vnew)) {
			err = -ENOTDIR;
			goto err_vnew;
		} else if(!VN_ISDIR(vold) && VN_ISDIR(vnew)) {
			err = -EISDIR;
			goto err_vnew;
		}
	}

	/* TODO CACHE */

	/*
	 * Renaming a node to itself is useless.
	 */
	if(vnew != vold) {
		vnode_lock(vold, VNLOCK_EXCL);
		if(vnew) {
			vnode_lock(vnew, VNLOCK_EXCL);
		}

		/*
		 * TODO make sure that old and new are not mountpoints,
		 * because that would break a lot of thinks
		 */
		err = vnode_rename(onamei.parent.node, onamei.childname,
			onamei.namelen, vold, nnamei.parent.node,
			nnamei.childname, nnamei.namelen, vnew);

		vnode_unlock(vold);
		if(vnew) {
			vnode_unlock(vnew);
		}

		if(!err) {
			/*
			 * Purge the cache.
			 */
			vdirent_cache_try_rem(onamei.parent.node,
				onamei.childname, onamei.namelen);
			vdirent_cache_try_rem(nnamei.parent.node,
				nnamei.childname, nnamei.namelen);

			if(VN_ISDIR(vold) &&
				onamei.parent.node != nnamei.parent.node)
			{
				vdirent_cache_try_rem(vold, "..", 2);
			}
		}
	}

err_vnew:
	if(vnew) {
		vnode_unref(vnew);
	}
err_dircheck:
	vnode_unref(vold);
err_find_old:
err_perm:
	vnode_unlock(onamei.parent.node);
	if(onamei.parent.node != nnamei.parent.node) {
		vnode_unlock(nnamei.parent.node);
	}
err_fs:
	vnamei_done(&nnamei);
err_namei_new:
	vnamei_done(&onamei);
	return err;
}

/*
 * Getting the full path of a directory is a nightmare!
 * vfs_dirpath builds up the path in the buffer in a reversed order
 * (since it starts at dir and walks up until root) and the strings
 * are also reversed (e.g. lacol/rsu/) and then the whole string is
 * reversed (resulting e.g. /usr/local)
 */
int vfs_dirpath(vpath_t *dir, char *buf, size_t buflen) {
	vpath_t parent = VPATH_INIT, cur = VPATH_INIT;
	char *old = buf;
	int err;

	assert(VN_ISDIR(dir->node));
	if(buflen < 2) { /* Minimum is "/" (2 bytes because of '\0') */
		return -EINVAL;
	}

	vpath_cpy(&cur, dir);
	while(!vfs_is_root(&cur)) {
		char namebuf[NAME_MAX + 1];
		size_t len;

		err = vlookup(&cur, "..", VNAMEI_DIR, &parent);
		if(err) {
			vpath_clear(&cur);
			return err;
		} else {
			assert(parent.node);
		}

		err = vnode_childname(parent.node, cur.node->ino, namebuf,
			sizeof(namebuf));
		vpath_clear(&cur);
		if(err) {
			vpath_clear(&parent);
			return err;
		}

		cur = parent;
		parent.node = NULL;
		parent.mount = NULL;

		strreverse(namebuf);
		len = strlen(namebuf) + 1; /* the +1 is for the '/' */
		if(buflen < len) {
			vpath_clear(&cur);
			return -ERANGE;
		}

		strcpy(buf, namebuf);
		buf[len - 1] = '/';

		buf += len;
		buflen -= len;
	}

	/*
	 * If dir is the proc's filesystem root, nothing has
	 * been written into the buffer yet.
	 */
	if(buf == old) {
		*buf = '/';
		buf++;
		buflen--;
	}

	vpath_clear(&cur);

	/*
	 * No space for the last '\0'.
	 */
	if(buflen == 0) {
		return -ERANGE;
	}

	*buf = '\0';
	strreverse(old);

	return 0;
}

int kern_mount(const char *source, const char *target, const char *type,
	unsigned long flags, const void *data)
{
	vpath_t tpath = VPATH_INIT;
	vmount_t *vmount;
	filesys_t *fs;
	ino_t mountino;
	int err;

	if(flags & ~(MS_RDONLY)) {
		return -EINVAL;
	}

	/*
	 * When the root filesystem is mounted, there is no target directory.
	 * Otherwise lookup the target directory.
	 */
	if(vpath_is_empty(&vfs_root)) {
		assert(target == NULL);
		mountino = 0;
	} else {
		err = vlookup(NULL, target, VNAMEI_DIR, &tpath);
		if(err) {
			return err;
		}

		mountino = tpath.node->ino;
	}

	vmount = vmount_alloc(mountino, F_ISSET(flags, MS_RDONLY) ?
		VMOUNT_RO : 0);

	/*
	 * See, whether something is already mounted on this directory.
	 */
	sync_acquire(&vfs_mountlock);
	if(!vpath_is_empty(&vfs_root) &&
		vmount_mountpoint_p(tpath.mount, tpath.node->ino))
	{
		err = -EBUSY;
		goto out;
	}

	err = filesys_mount(source, type, flags, data, &fs);
	if(err) {
		goto out;
	}

	if(vpath_is_empty(&vfs_root)) {
		err = filesys_vget(fs, fs->root, &vfs_root.node);
		if(err) {
			filesys_unref(fs); /* TODO */
			goto out;
		}

		vfs_root.mount = vmount_ref(vmount);
	}

	/*
	 * Insert the new mount into the mount tree.
	 */
	vmount_insert(tpath.mount, vmount, fs, fs->root);
	filesys_unref(fs);

out:
	sync_release(&vfs_mountlock);
	vmount_unref(vmount);
	vpath_clear(&tpath);
	return err;
}

void __init init_vfs(void) {
	int err;

	vcache_init();

	/*
	 * Mount the initrd as the root directory.
	 */
	err = kern_mount(NULL, NULL, "initrd", MS_RDONLY, NULL);
	if(err) {
		kpanic("[vfs] could not mount root");
	}

	/*
	 * Mount a devfs instance. Another devfs instance will
	 * be mounted once the real fs will be mounted.
	 */
	err = kern_mount(NULL, "/dev", "devfs", 0, NULL);
	if(err) {
		kpanic("[vfs] failed to mount devfs: %d", err);
	}
}

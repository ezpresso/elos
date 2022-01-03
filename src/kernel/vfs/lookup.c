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
#include <vm/malloc.h>
#include <vfs/lookup.h>
#include <vfs/vnode.h>
#include <vfs/vpath.h>
#include <vfs/vcache.h>
#include <vfs/vdirent.h>
#include <vfs/proc.h>
#include <vfs/fs.h>
#include <vfs/uio.h>
#include <lib/string.h>
#include <sys/param.h>
#include <sys/limits.h>

#define VNAMEI_MAY_DIR(flags) F_ISSET(flags, VNAMEI_DIR | VNAMEI_ANY)

static int vnamei_internal(vpath_t *at, const char *path, vnamei_op_t op,
		int flags, size_t depth, vnamei_t *namei);

static int vnamei_check_type(vnode_t *node, int flags) {
	if(!VNAMEI_MAY_DIR(flags) && VN_ISDIR(node)) {
		return -EISDIR;
	} else if(F_ISSET(flags, VNAMEI_DIR) && !VN_ISDIR(node)) {
		return -ENOTDIR;
	} else {
		return 0;
	}
}

static int vfs_reslove_mount(vpath_t *path, vnamei_op_t op) {
	vmount_t *mount;
	vnode_t *node;
	int err;

	mount = vmount_lookup(path->mount, path->node->ino);
	if(mount == NULL) {
		return 0;
	}

	if(op == VNAMEI_UNLINK) {
		vmount_unref(mount);
		return -EBUSY;
	} else if(op == VNAMEI_CREATE) {
		vmount_unref(mount);
		return -EEXIST;
	}

	err = filesys_vget(mount->filesys, mount->root, &node);
	if(err) {
		vmount_unref(mount);
		return err;
	}

	vpath_clear(path);
	path->mount = mount;
	path->node = node;

	return 0;
}

static int vnamei_vnode(vpath_t *parent, const char *name, size_t namelen,
	vnamei_op_t op, int flags, vnamei_t *namei, vnode_t **nodep)
{
	vnode_t *node = parent->node;
	vdirent_t *dirent;
	int err = 0, acc;
	ino_t ino;

	if(!VN_ISDIR(node)) {
		return -ENOTDIR;
	}

	/*
	 * The execute permission means search permission for directories.
	 */
	acc = VN_ACC_X;
	if(op != VNAMEI_LOOKUP) {
		acc |= VN_ACC_W;
	}

	vnode_lock(node, VNLOCK_EXCL);
	if(!vnode_access(node, acc)) {
		vnode_unlock(node);
		return -EACCES;
	}

	dirent = vdirent_cache_lookup(node, name, namelen);
	if(!dirent || op == VNAMEI_UNLINK) {
		/*
		 * Ask the filesystem for help, if the dirent wasn't in
		 * the cache or if the dirent is going to be unlinked (the
		 * fs saves information about the position of the dirent,
		 * which is needed in a subsequent call to vnode_unlink)
		 */
		err = vnode_namei(node, name, namelen, op, &ino);
		if(!err && op != VNAMEI_UNLINK) {
			vdirent_cache_new(node, name, namelen, ino, 0);
		}
	} else {
		ino = dirent->ino;
	}

	if(!err && F_ISSET(flags, VNAMEI_EXCL)) {
		err = -EEXIST;
	}

	if(F_ISSET(flags, VNAMEI_LOCKPARENT) && (err == 0 ||
		(err == -ENOENT && F_ISSET(flags, VNAMEI_OPTIONAL))))
	{
		/*
		 * Return the parent in a !!locked!! state. Thus no
		 * no vnode_unlock is called.
		 */
		vpath_cpy(&namei->parent, parent);
		namei->dirent = dirent;
	} else {
		if(dirent) {
			vdirent_cache_put(node, dirent);
		}

		vnode_unlock(node);
	}

	if(err == 0) {
		err = filesys_vget(node->fs, ino, nodep);
	} else if(err == -ENOENT && F_ISSET(flags, VNAMEI_OPTIONAL)) {
		*nodep = NULL;
		err = 0;
	}

	return err;
}

static int vfs_resolve_symlink(vpath_t *path, vnode_t *node, size_t depth) {
	vnamei_t namei;
	ssize_t res;
	char *buf;

	vpath_init(&namei.path);
	vpath_init(&namei.parent);
	namei.dirent = NULL;
	namei.childname = NULL;

	if(depth >= MAXSYMLINKS) {
		return -ELOOP;
	}

	buf = kmalloc(PATH_MAX, VM_NOFLAG);
	if(buf == NULL) {
		return -ENOMEM;
	}

	res = vnode_readlink(node, buf, PATH_MAX, KERNELSPACE);
	if(res > 0) {
		res = vnamei_internal(path, buf, VNAMEI_LOOKUP, VNAMEI_ANY,
			depth + 1, &namei);
		if(res == 0) {
			vpath_clear(path);
			*path = namei.path;
		}
	} else if(res == 0) {
		res = -ENOENT;
	}

	kfree(buf);
	return res;
}

static int vfs_do_get_child(vpath_t *ipath, const char *name, size_t namelen,
	int flags, vnamei_op_t op, size_t depth, vnamei_t *namei,
	vpath_t *path)
{
	bool dotdot = false;
	vnode_t *node;
	int err;

	if(path) {
		ASSERT_VPATH_NULL(path);
	}

	/*
	 * When going to the parent directory check if the node is the root
	 * of the mount and go to the superordinate mount if necessary.
	 */
	if(namelen == 2 && !strcmp(name, "..")) {
		if(!VNAMEI_MAY_DIR(flags)) {
			return -EISDIR;
		} else if(op == VNAMEI_CREATE) {
			return -EEXIST;
		} else if(op == VNAMEI_UNLINK) {
			return -ENOTEMPTY;
		}

		if(vfs_is_root(ipath)) {
			vpath_cpy(path, ipath);
			return 0;
		} else if(ipath->mount->root == ipath->node->ino) {
			vmount_t *parent;
			vnode_t *node;

			/*
			 * You cannot go up further than the filesystem root.
			 * This case can only happen if a process's cwd is still
			 * outside of the chroot jail, because otherwise the
			 * check for vfs_is_root() above would disallow this
			 * case.
			 */
			if(ipath->mount->parent == NULL) {
				vpath_cpy(path, ipath);
				return 0;
			}

			/*
			 * Look for the point in the superordinate mount
			 * where this mount was mounted and set _path_
			 * accordingly.
			 */
			parent = ipath->mount->parent;
			err = filesys_vget(parent->filesys,
				ipath->mount->mountpoint, &node);
			if(err) {
				return err;
			}

			path->mount = vmount_ref(parent);
			path->node = node;
		} else {
			vpath_cpy(path, ipath);
		}

		dotdot = true;
	} else if(namelen == 1 && !strcmp(name, ".")) {
		if(!VNAMEI_MAY_DIR(flags)) {
			return -EISDIR;
		} else if(op == VNAMEI_UNLINK) {
			return -EINVAL;
		} else if(op == VNAMEI_CREATE) {
			return -EEXIST;
		}

		vpath_cpy(path, ipath);
		return 0;
	} else if(path) {
		vpath_cpy(path, ipath);
	}

	/*
	 * Find the child node.
	 */
	err = vnamei_vnode(path, name, namelen, op, flags, namei, &node);
	if(err) {
		goto error;
	}

	if(node == NULL) {
		vpath_clear(path);
	} else if(VN_ISLNK(node) && !F_ISSET(flags, VNAMEI_NOFOLLOW)) {
		assert(op == VNAMEI_LOOKUP);
		assert(!(flags & VNAMEI_LOCKPARENT));

		err = vfs_resolve_symlink(path, node, depth);
		if(err) {
			goto error;
		}

		vnode_unref(node);
	} else {
		/*
		 * Update the node in the path, but the mount does not have
		 * to be updated.
		 */
		vnode_unref(path->node);
		path->node = node;

		/*
		 * Don't look for child mounts if ".." is being looked up
		 */
		if(dotdot == false && VN_ISDIR(node)) {
			if(F_ISSET(flags, VNAMEI_LOCKPARENT |
				VNAMEI_WANTPARENT))
			{
				/*
				 * If op is LOOKUP than vfs_reslove_mount
				 * would not fail (getting the parent does
				 * not work if the mountpoint is a child)
				 */
				assert(op != VNAMEI_LOOKUP);
			}

			err = vfs_reslove_mount(path, op);
			if(err) {
				goto error;
			}
		}
	}

	return 0;

error:
	vpath_clear(path);
	return err;
}

static int vnamei_internal(vpath_t *at, const char *path, vnamei_op_t op,
	int flags, size_t depth, vnamei_t *namei)
{
	vpath_t vpath = VPATH_INIT;
	size_t idx, last = 0, length;
	char *buf = NULL;
	int err = 0;

	/*
	 * Make sure that namei was initialized correctly.
	 */
	assert(namei->dirent == NULL);
	assert(namei->childname == NULL);
	ASSERT_VPATH_NULL(&namei->path);
	ASSERT_VPATH_NULL(&namei->parent);

	length = strlen(path);
	if(length > 0) {
		last = length - 1;

		/*
		 * Ignore the slashes at the end of the path
		 * (e.g. /usr/local///).
		 */
		while(path[last] == '/') {
			/*
			 * If there is at least one slash at the end of the
			 * path (e.g. /usr/local/), the node has to be a
			 * directory
			 */
			if(!VNAMEI_MAY_DIR(flags)) {
				return -ENOENT;
			}

			/*
			 * If the path only consists of slashes (e.g. /////)
			 * return the process's root.
			 */
			if(last == 0) {
				vfs_get_root(&vpath);
				goto out;
			}

			last--;
		}
	}

	if(path[0] == '/') {
		path++;
		last--;
		vfs_get_root(&vpath);
	} else if(at && !vpath_is_empty(at)) {
		vpath_cpy(&vpath, at);
	} else {
		vfs_get_cwd(&vpath);
	}

	/*
	 * POSIX says that empty path is not valid (if VNAMEI_EMPTY
	 * is not set).
	 */
	if(length == 0) {
		if(!(flags & VNAMEI_EMPTY)) {
			err = -ENOENT;
		}

		goto out;
	} else if(last == 0 && path[0] == '\0') {
		if(op == VNAMEI_CREATE) {
			err = -EEXIST;
		} else if(op == VNAMEI_UNLINK) {
			if(!VNAMEI_MAY_DIR(flags)) {
				err = -EISDIR;
			} else {
				err = -EBUSY;
			}
		} else {
			assert(op == VNAMEI_LOOKUP);
		}

		if(err){
			goto out;
		}
	}

	/*
	 * Allocate memory for one path element. This is
	 * used later when splitting the path into the
	 * individual path elements.
	 */
	buf = kmalloc(NAME_MAX + 1, VM_NOFLAG);
	if(!buf) {
		goto out;
	}

	idx = 0;
	while(idx <= last) {
		const char *cpath = &path[idx];
		vpath_t next = VPATH_INIT;
		vnamei_op_t cur_op;
		int cur_flags;
		char *delim;
		size_t len;

		/*
		 * vfs_do_get_child may return an empty path during the
		 * lookup of the last segment (see VNAMEI_OPTIONAL). Make
		 * sure that after it returned an empty path, it does not
		 * continue.
		 */
		assert(vpath.node);
		assert(vpath.mount);

		/*
		 * If the current path is in the middle of the path and
		 * it does not refer to a directory, abort the search.
		 */
		if(!VN_ISDIR(vpath.node)) {
			err = -ENOTDIR;
			break;
		}

		/*
		 * Get the next path element.
		 */
		delim = strchr(cpath, '/');
		if(delim) {
			len = delim - cpath;
		} else {
			len = strlen(cpath);
		}

		len = min(len, (last + 1) - idx);
		if(len == 0) {
			assert(idx != last);
			/*
			 * len == 0 if path is '/usr//bin' for example.
			 */
			idx++;
			continue;
		}

		if(len > NAME_MAX) {
			err = -ENAMETOOLONG;
			break;
		}

		memcpy(buf, cpath, len);
		buf[len] = '\0';

		/*
		 * The '+ 1' is there to skip the path separator.
		 */
		idx = min(last + 1, (idx + len + 1));

		if(idx <= last) {
			cur_flags = VNAMEI_DIR;
			cur_op = VNAMEI_LOOKUP;
		} else if(flags & VNAMEI_WANTPARENT) {
			namei->parent = vpath;
			namei->childname = buf;
			namei->namelen = len;

			/*
			 * Make sure that _buf_ is not being freed.
			 */
			buf = NULL;

			vpath.node = NULL;
			vpath.mount = NULL;
			break;
		} else {
			cur_flags = flags;
			cur_op = op;
		}

		err = vfs_do_get_child(&vpath, buf, len, cur_flags,
			cur_op, depth, namei, &next);
		if(err) {
			break;
		}

		if(cur_flags & VNAMEI_LOCKPARENT) {
			namei->childname = buf;
			namei->namelen = len;

			/*
			 * Make sure that _buf_ is not being freed.
			 */
			buf = NULL;
		}

		vpath_clear(&vpath);
		vpath = next;
	}

out:
	if(!err) {
		if(F_ISSET(flags, VNAMEI_LOCKPARENT)) {
			assert(!vpath_is_empty(&namei->parent));
			assert(namei->childname);
			VN_ASSERT_LOCK_WR(namei->parent.node);
		} else if(F_ISSET(flags, VNAMEI_WANTPARENT)) {
			assert(!vpath_is_empty(&namei->parent));
			assert(namei->childname);
			ASSERT_VPATH_NULL(&vpath);
		}
	}

	if(!err && !vpath_is_empty(&vpath)) {
		err = vnamei_check_type(vpath.node, flags);
	}

	if(err) {
		vpath_clear(&vpath);
		if(flags & VNAMEI_LOCKPARENT &&
			!vpath_is_empty(&namei->parent))
		{
			vnode_unlock(namei->parent.node);
		}

		vpath_clear(&namei->parent);
	} else {
		namei->path = vpath;
	}

	if(buf) {
		kfree(buf);
	}

	return err;
}

int vnamei(vpath_t *at, const char *path, int flags, vnamei_op_t op,
	vnamei_t *namei)
{
	int err;

	assert(op != VNAMEI_RENAME);
	if(F_ISSET(flags, VNAMEI_EROFS)) {
		assert(!F_ISSET(flags, VNAMEI_OPTIONAL));
	}

	namei->path = VPATH_INIT;
	namei->parent = VPATH_INIT;
	namei->dirent = NULL;
	namei->childname = NULL;

	err = vnamei_internal(at, path, op, flags, 0, namei);
	if(err == 0) {
		if(F_ISSET(flags, VNAMEI_LOCKPARENT)) {
			assert(namei->parent.node != NULL);
			VN_ASSERT_LOCK_WR(namei->parent.node);
		} else if(F_ISSET(flags, VNAMEI_WANTPARENT)) {
			assert(namei->parent.node != NULL);
			assert(namei->childname);
			ASSERT_VPATH_NULL(&namei->path);
		}

		if((F_ISSET(flags, VNAMEI_WANTPARENT) ||
			F_ISSET(flags, VNAMEI_LOCKPARENT)) &&
			!vpath_is_empty(&namei->path))
		{
			assert(namei->parent.mount == namei->path.mount);
		}

		if(F_ISSET(flags, VNAMEI_EROFS) &&
			((F_ISSET(flags, VNAMEI_WANTPARENT) &&
			!vmount_writeable(namei->parent.mount)) ||
			!vmount_writeable(namei->path.mount)))

		{
			err = -EROFS;
			if(F_ISSET(flags, VNAMEI_LOCKPARENT)) {
				vnamei_done_unlock(namei);
			} else {
				vnamei_done(namei);
			}
		}
	}

	return err;
}

void vnamei_done(vnamei_t *namei) {
	vpath_clear(&namei->parent);
	vpath_clear(&namei->path);
	namei->dirent = NULL;

	if(namei->childname) {
		kfree(namei->childname);
		namei->childname = NULL;
	}
}

void vnamei_done_unlock(vnamei_t *namei) {
	assert(namei->parent.node);

	if(namei->dirent) {
		vdirent_cache_put(namei->parent.node, namei->dirent);
	}

	vnode_unlock(namei->parent.node);
	vnamei_done(namei);
}

int vlookup(struct vpath *at, const char *path, int flags,
	struct vpath *result)
{
	vnamei_t namei;
	int err;

	err = vnamei(at, path, flags, VNAMEI_LOOKUP, &namei);
	if(err == 0) {
		*result = namei.path;

		/*
		 * Don't drop the reference counter for path on vnamei_done.
		 */
		namei.path = VPATH_INIT;
		vnamei_done(&namei);
	}

	return err;
}

int vlookup_node(vpath_t *at, const char *path, int flags, vnode_t **out) {
	vpath_t vp = VPATH_INIT;
	int err;

	err = vlookup(at, path, flags, &vp);
	if(err == 0) {
		*out = vp.node;
		vmount_unref(vp.mount);
	}

	return err;
}

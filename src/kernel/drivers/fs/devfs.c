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
#include <kern/proc.h> /* UID_ROOT TODO */
#include <vfs/fs.h>
#include <vfs/vnode.h>
#include <vfs/vcache.h>
#include <vfs/vdirent.h>
#include <vfs/lookup.h>
#include <vfs/uio.h>
#include <vfs/devfs.h>
#include <vm/malloc.h>
#include <lib/tree.h>
#include <lib/string.h>
#include <lib/callback.h>
#include <sys/dirent.h>
#include <sys/statfs.h>
#include <sys/limits.h>

typedef struct devfs_node {
	tree_node_t tnode;
	vnode_t vnode;
	vdirent_t *dirent;
} devfs_node_t;

static devfs_node_t devfs_root;
static filesys_t *devfs_fs;

static vop_namei_t devfs_namei;
static vop_getdents_t devfs_getdents;
static vnode_ops_t devfs_vnops = {
	.read =		VOP_PANIC,
	.write =	VOP_PANIC,
	.truncate =	CB(EPERM),
	.unlink =	VOP_PANIC,
	.rename =	VOP_PANIC,
	.readlink =	VOP_PANIC,
	.bmap =		VOP_PANIC,
	.sync =		VOP_PANIC,
	.pagein =	VOP_PANIC,
	.pageout =	VOP_PANIC,
	.create =	VOP_PANIC,
	.set_exe =	VOP_PANIC,
	.unset_exe =	VOP_PANIC,
	.namei =	devfs_namei,
	.getdents =	devfs_getdents,
};

static int devfs_namei(vnode_t *node, const char *name, size_t namelen,
	vnamei_op_t op, ino_t *ino)
{
	devfs_node_t *inode = vnode_priv(node), *parent;

	if(op != VNAMEI_LOOKUP) {
		return -ENOTSUP;
	}

	if(namelen == 2 && !strcmp(name, "..")) {
		assert(VN_ISDIR(node));
		parent = tree_parent(&inode->tnode);
		assert(parent); 
		*ino = parent->vnode.ino;
		return 0;
	} else {
		/*
		 * Directory entries other than dotdot are in the vdirent
		 * cache and thus a the lookup callback won't be called
		 * when looking up existing dirents.
		 */
		assert(strcmp(name, "."));
		return -ENOENT;
	}
}

static int devfs_getdents(vnode_t *node, uio_t *uio) {
	devfs_node_t *inode = vnode_priv(node);
	size_t done = 0, namelen;
	struct kdirent dirent;
	ssize_t res;
	off_t off;

	dirent.d_type = DT_UNKNOWN;
	off = uio->off;

	while(uio->size >= DIRENT_SZ) {
		devfs_node_t *child;
		const char *name;

		switch(off) {
		case 0:
			child = inode;
			name = ".";
			break;
		case 1:
			/*
			 * The first second directory entry is "..", which
			 * describes the parent node.
			 */
			child = tree_parent(&inode->tnode);
			if(child == NULL) {
				/*
				 * The root does not have a parent.
				 */
				child = inode;
			}
			name = "..";
			break;
		default:
			child = tree_get(&inode->tnode, off - 2);
			if(child == NULL) {
				goto out;
			}

			name = vdirent_name(child->dirent);
			break;
		}

		namelen = strlen(name);
	
		/*
		 * Fill out the dirent structure.
		 */
		dirent.d_off = off + 1; /* offset to next structure */
		dirent.d_ino = child->vnode.ino;
		dirent.d_reclen = DIRENT_LEN(namelen);
		if(uio->size < dirent.d_reclen) {
			break;
		}

		res = uiodirent(&dirent, name, namelen, uio);
		if(res) {
			return res;
		}

		done += dirent.d_reclen;
		off++;

		/*
		 * Adjust the uio->off since this function uses
		 * indexes as offsets and uiomove increments off
		 * by DIRENT_SZ + namelen.
		 */
		uio->off = off;
	}

out:
	return done;
}

static void devfs_node_init(devfs_node_t *node, dev_t dev, mode_t mode,
	size_t blksz_shift)
{
	tree_node_init(node, &node->tnode);
	
	/*
	 * Initialize the vnode.
	 */
	vnode_init(&node->vnode, devfs_fs, &devfs_vnops);
	node->vnode.mode = mode;
	node->vnode.rdev = dev;
	node->vnode.uid = UID_ROOT;
	node->vnode.gid = UID_ROOT;
	node->vnode.blksz_shift = blksz_shift;
	node->vnode.priv = node;
	node->vnode.ino = (ino_t)(uintptr_t)node;
	node->vnode.blocks = 0;

	wrlocked(&node->vnode.statlock) {
		vnode_settime(&node->vnode, VN_CURTIME, VN_ATIME | VN_MTIME |
			VN_CTIME);
	}

	if(S_ISDIR(mode)) {
		node->vnode.nlink = 2;
	} else {
		node->vnode.nlink = 1;
	}

	synchronized(&devfs_fs->lock) {
		vnode_cache_add(&node->vnode);
	}
}

devfs_node_t *devfs_mknodat(devfs_node_t *parent, dev_t dev, mode_t mode,
	size_t blksz_shift, const char *name)
{
	devfs_node_t *node;

	if(parent == NULL) {
		parent = &devfs_root;
	}

	node = kmalloc(sizeof(*node), VM_WAIT);
	devfs_node_init(node, dev, mode, blksz_shift);
	node->dirent = vdirent_alloc(devfs_fs, name, strlen(name),
		parent->vnode.ino, node->vnode.ino, VD_PERM, VM_WAIT);

	/*
	 * Insert the new node into the devfs tree.
	 */
	wrlocked(&parent->vnode.lock) {
		vdirent_cache_add(&parent->vnode, node->dirent);
		tree_insert(&parent->tnode, &node->tnode);
	}

	return node;
}

struct devfs_node *devfs_mkdir(struct devfs_node *parent, const char *name) {
	return devfs_mknodat(parent, 0, S_IFDIR | 0777, 0, name);
}

void devfs_rmnod(devfs_node_t *node) {
	devfs_node_t *parent;

	assert(tree_childnum(&node->tnode) == 0);
	assert(node != &devfs_root);

	/*
	 * Remove the node from the devfs tree.
	 */
	parent = tree_parent(&node->tnode);
	wrlocked(&parent->vnode.lock) {
		tree_remove(&parent->tnode, &node->tnode);
		vdirent_cache_rem(&parent->vnode, node->dirent);
		node->dirent = NULL;
	}

	/*
	 * It is possible that a directory entry ".." is still
	 * in the cache of the directory.
	 */
	if(VN_ISDIR(&node->vnode)) {
		wrlock_scope(&node->vnode.lock);
		/*
		 * The "." should not be cached here, but...
		 */
		vdirent_cache_try_rem(&node->vnode, ".", 1);
		vdirent_cache_try_rem(&node->vnode, "..", 2);
	}

	node->vnode.nlink = 0;
	vnode_unref(&node->vnode);
}

static int devfs_mount(filesys_t *fs, ino_t *root) {
	assert(!devfs_fs);

	devfs_fs = fs;
	devfs_node_init(&devfs_root, 0, S_IFDIR | 0777, 0);
	*root = devfs_root.vnode.ino;

	return 0;
}

int devfs_unmount(__unused filesys_t *fs) {
	kpanic("devfs unmount");
	return 0;
}

static int devfs_vget(filesys_t *fs, ino_t ino, vnode_t **node) {
	*node = vnode_cache_lookup(fs, ino);
	assert(*node);
	return 0;
}

static void devfs_vput(__unused filesys_t *fs, vnode_t *node) {
	devfs_node_t *inode = vnode_priv(node);

	tree_node_destroy(&inode->tnode);
	vnode_destroy(&inode->vnode);
	kfree(inode);
}

static void devfs_statfs(filesys_t *fs, struct statfs *stat) {
	(void) fs;

	/* TODO */
	stat->f_type = 0;
	stat->f_bsize = 1; /* ??? */
	stat->f_blocks = 0;
	stat->f_bfree = 0;
	stat->f_bavail = 0;
	stat->f_files = 0;
	stat->f_ffree = 0;
	stat->f_fsid.val[0] = 0;
	stat->f_fsid.val[1] = 0;
	stat->f_namelen = NAME_MAX;
	stat->f_frsize = 0;
	stat->f_flags = 0;
}

static FS_DRIVER(devfs_driver) {
	.name = "devfs",
	.flags = FSDRV_SINGLETON,
	.mount = devfs_mount,
	.vget = devfs_vget,
	.vput = devfs_vput,
	.statfs = devfs_statfs,
};

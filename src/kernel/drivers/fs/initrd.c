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
#include <kern/multiboot.h>
#include <vm/malloc.h>
#include <vm/page.h>
#include <vm/kern.h>
#include <vm/object.h>
#include <vm/phys.h> /* vm_page_phys */
#include <vfs/vcache.h>
#include <vfs/vnode.h>
#include <vfs/vfs.h>
#include <vfs/lookup.h>
#include <vfs/fs.h>
#include <vfs/uio.h>
#include <vfs/vpath.h>
#include <vfs/vmount.h>
#include <vfs/uio.h>
#include <lib/tree.h>
#include <lib/tar.h>
#include <lib/string.h>
#include <sys/stat.h>
#include <sys/limits.h>
#include <sys/dirent.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/mount.h>

#define INITRD_MODE	0500 /* owner: read, exec */
#define INITRD_BLKSHIFT	PAGE_SHIFT
#define INITRD_ROOT	1
#define INITRD_UID	0
#define INITRD_GID	0

/*
 * This is a node in the initrd's filesystem tree.
 * Since there are no hardlinks in the init-ramdisk,
 * no dirent structures are needed.
 */
typedef struct initrd_inode {
	vnode_t vnode;
	tree_node_t node;

	/*
	 * A pointer to the data being in the file.
	 * This is NULL for directories.
	 */
	void *buffer;
	ino_t ino;

	size_t namelen;
	char name[];
} initrd_inode_t;

/*
 * The initrd vnode interface
 */
static vop_open_t 	initrd_open;
static vop_rdwr_t	initrd_read;
static vop_namei_t 	initrd_namei;
static vop_getdents_t	initrd_getdents;
static vop_pagein_t	initrd_pagein;
static vnode_ops_t initrd_ops = {
	.open =		initrd_open,
	.read =		initrd_read,
	.namei =	initrd_namei,
	.getdents =	initrd_getdents,
	.pagein =	initrd_pagein,
	.pageout =	VOP_PANIC,

	.truncate =	VOP_PANIC,
	.unlink =	VOP_PANIC,
	.rename =	VOP_PANIC,
	.write =	VOP_PANIC,
	.readlink =	VOP_PANIC,
	.bmap =		VOP_PANIC,
	.create =	VOP_PANIC,

	/*
	 * TODO vnode.c code calls vnode_dirtd and vnode_sync
	 * => make sure that vnode_sync (or actually vnode_dirty) does
	 * not work for rofs
	 */
	.sync =		VOP_PANIC,
	.set_exe =	vop_generic_set_exe,
	.unset_exe =	vop_generic_unset_exe,
};

static ino_t initrd_ino = INITRD_ROOT;

static int initrd_open(__unused vnode_t *node, __unused int flags) {
	return 0;
}

static ssize_t initrd_read(vnode_t *node, uio_t *uio) {
	initrd_inode_t *inode = vnode_priv(node);
	return uiomove(inode->buffer + uio->off, uio->size, uio);
}

static int initrd_pagein(vnode_t *node, vm_objoff_t off, vm_page_t *page) {
	initrd_inode_t *inode = vnode_priv(node);
	size_t size;
	void *map;

	size = min(PAGE_SZ, node->size - off);
	map = vm_kern_map_quick(vm_page_phys(page));
	memcpy(map, inode->buffer + off, size);
	memset(map + size, 0x0, PAGE_SZ - size);
	vm_kern_unmap_quick(map);

	return 0;
}

static int initrd_namei(vnode_t *node, const char *name, size_t namelen,
	__unused vnamei_op_t op, ino_t *ino)
{
	initrd_inode_t *inode = vnode_priv(node), *cur;

	assert(strcmp(name, "."));
	if(namelen == 2 && !strcmp(name, "..")) {
		cur = tree_parent(&inode->node);
		if(cur == NULL) {
			*ino = node->ino;
		} else {
			*ino = cur->ino;
		}

		return 0;
	}

	/*
	 * Search the children of the inode.
	 */
	tree_foreach(cur, &inode->node) {
		if(cur->namelen == namelen && !strcmp(cur->name, name)) {
			*ino = cur->ino;
			return 0;
		}
	}

	return -ENOENT;
}

static int initrd_getdents(vnode_t *node, uio_t *uio) {
	initrd_inode_t *inode = vnode_priv(node);
	size_t done = 0, namelen;
	struct kdirent dirent;
	ssize_t res;
	off_t off;

	dirent.d_type = DT_UNKNOWN;
	off = uio->off;

	while(uio->size >= DIRENT_SZ) {
		initrd_inode_t *child;
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
			child = tree_parent(&inode->node);
			if(child == NULL) {
				/*
				 * The root does not have a parent.
				 */
				child = inode;
			}

			name = "..";
			break;
		default:
			child = tree_get(&inode->node, off - 2);
			if(child == NULL) {
				return done;
			}

			name = child->name;
		}

		namelen = strlen(name);

		/*
		 * Fill out the dirent structure.
		 */
		dirent.d_off = off + 1;
		dirent.d_ino = child->ino;
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

	return done;
}

static void initrd_statfs(filesys_t *fs, struct statfs *stat) {
	(void) fs;

	/* TODO */
	stat->f_type = 0;
	stat->f_bsize = PAGE_SZ;
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

static initrd_inode_t *initrd_alloc_node(filesys_t *fs, initrd_inode_t *parent,
	const char *name, size_t namelen, mode_t mode)
{
	initrd_inode_t *inode;
	ino_t ino;

	/*
	 * Get a new inode number.
	 */
	ino = initrd_ino++;
	inode = kmalloc(sizeof(*inode) + namelen + 1, VM_WAIT);

	/*
	 * Initialize the inode.
	 */
	tree_node_init(inode, &inode->node);
	inode->namelen = namelen;
	inode->buffer = NULL;
	inode->ino = ino;
	if(name != NULL) {
		strcpy(inode->name, name);
	}

	/*
	 * Make the inode a child of the parent.
	 */
	if(parent != NULL) {
		tree_insert(&parent->node, &inode->node);
	}

	/*
	 * Initialize the vnode.
	 */
	vnode_init(&inode->vnode, fs, &initrd_ops);
	inode->vnode.priv = inode;
	inode->vnode.ino = ino;
	inode->vnode.mode = mode | INITRD_MODE;
	inode->vnode.uid = INITRD_UID;
	inode->vnode.gid = INITRD_GID;
	inode->vnode.blksz_shift = INITRD_BLKSHIFT;
	inode->vnode.flags |= VN_PERM;
	inode->vnode.rdev = 0;
	inode->vnode.blocks = 0;
	inode->vnode.nlink = S_ISDIR(mode) ? 2 : 1;

	/*
	 * Make sure the sync_assert from vcache does not fail.
	 */
	synchronized(&fs->lock) {
		/*
		 * Add the new node to the cache.
		 */
		vnode_cache_add(&inode->vnode);
	}

	return inode;
}

static int initrd_parse(filesys_t *fs, vpath_t *at, void *initrd) {
	initrd_inode_t *inode;
	tar_header_t *hdr;
	vnamei_t namei;
	size_t size;
	mode_t mode;
	char *path;
	int err;

	/*
	 * Iterate through the headers and create the appropriate
	 * filesystem nodes.
	 */
	for(hdr = initrd; hdr != NULL; hdr = tar_next_header(hdr)) {
		if(strcmp(hdr->magic, TMAGIC) != 0) {
			break;
		}

		path = hdr->name;
		if(!strcmp(path, "/")) {
			continue;
		} else if(path[0] == '/') {
			path++;
		}

		kprintf("[initrd] creating: \"%s\"\n", hdr->name);
		if(hdr->typeflag != REGTYPE && hdr->typeflag != DIRTYPE) {
			kprintf("[initrd] warning: unsuported file type: %c\n",
				hdr->typeflag);
			continue;
		}

		/*
		 * Lookup the parent filesystem node.
		 */
		err = vnamei(at, path, VNAMEI_LOCKPARENT | VNAMEI_EXCL |
			VNAMEI_ANY | VNAMEI_OPTIONAL, VNAMEI_LOOKUP, &namei);
		if(err) {
			return err;
		}

		if(hdr->typeflag == REGTYPE) {
			mode = S_IFREG;
		} else {
			mode = S_IFDIR;
		}

		/*
		 * Create the child node.
		 */
		inode = initrd_alloc_node(fs, namei.parent.node->priv,
			namei.childname, namei.namelen, mode);
		vnamei_done_unlock(&namei);

		if(hdr->typeflag == REGTYPE) {
			size = tar_get_size(hdr);
			inode->buffer = tar_get_data(hdr);
			inode->vnode.blocks = ALIGN(size, PAGE_SZ) >>
				INITRD_BLKSHIFT;
			vnode_init_size(&inode->vnode, size);
		}
	}

	return 0;
}

static int initrd_mount(filesys_t *fs, ino_t *rootino) {
	initrd_inode_t *inode;
	vpath_t root;
	void *initrd;
	int err;

	if(!F_ISSET(fs->flags, MS_RDONLY)) {
		return -EINVAL;
	}

	initrd = multiboot_module("initrd", NULL);
	if(initrd == NULL) {
		return -ENXIO;
	}

	/*
	 * Allocate the root vnode and setup vfs_root.
	 */
	inode = initrd_alloc_node(fs, NULL, NULL, 0, S_IFDIR);

	/*
	 * Since the tar file does not have tree structured nodes
	 * we have to build that tree for ourselves. We don't want
	 * to do this manually, but the problem when using the vnamei
	 * interface is that vnamei would require the initrd to already
	 * be in the filesystem tree and this does not work, becuase
	 * were currently mounting it. However we can setup a fake
	 * vpath_t and use this as the starting point for vnamei
	 * lookups and file creation. One hash to make sure the paths
	 * are relative by removing the '/' at the beginning of
	 * the path if there is one.
	 */
	root.node = &inode->vnode;
	root.mount = vmount_alloc(0, 0);
	root.mount->filesys = fs;
	root.mount->root = inode->ino;

	/*
	 * Parse the initrd tar file and build the corresponding
	 * directories and files.
	 */
	err = initrd_parse(fs, &root, initrd);
	if(err) {
		kpanic("[initrd] parsing failed: %d", err);
	}
	/*
	 * The fake root vpath is not needed anymore.
	 */
	root.mount->filesys = NULL;
	vmount_free(root.mount);

	*rootino = inode->ino;

	return 0;
}

static void initrd_unmount(__unused filesys_t *fs) {
	return;
}

static int initrd_vget(filesys_t *fs, ino_t ino, vnode_t **result) {
	vnode_t *node;

	node = vnode_cache_lookup(fs, ino);
	if(node == NULL) {
		kpanic("[initrd] vget: unknown inode");
	}

	*result = node;
	return 0;
}

static FS_DRIVER(initrd_drv) {
	.name = "initrd",
	.mount = initrd_mount,
	.unmount = initrd_unmount,
	.vget = initrd_vget,
	.statfs = initrd_statfs,
};

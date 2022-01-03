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
#include <kern/time.h>
#include <vfs/fs.h>
#include <vfs/uio.h>
#include <vfs/vcache.h>
#include <vfs/lookup.h>
#include <ext2/ext2.h>
#include <block/block.h>
#include <vm/malloc.h>
#include <lib/string.h>
#include <sys/dirent.h>
#include <sys/fcntl.h> /* AT_REMOVEDIR */

typedef struct ext2_emptydir {
	struct {
		uint32_t inode;
		uint16_t rec_len;
		uint8_t  name_len;
		uint8_t  file_type;
		char 	 name[4];
	} dent[2];
} ext2_emptydir_t;

static vop_create_t	ext2_create;
static vop_unlink_t	ext2_unlink;
static vop_getdents_t	ext2_getdents;
static vop_namei_t	ext2_namei;
static vop_rename_t	ext2_rename;
static vop_truncate_t	ext2_truncate;
static vop_bmap_t 	ext2_bmap;
static vop_sync_t	ext2_vn_sync;
static vop_readlink_t 	ext2_readlink;
vnode_ops_t ext2_vnops = {
	.read =		vop_generic_rdwr,
	.write =	vop_generic_rdwr,
	.pagein =	vop_generic_pagein,
	.pageout =	vop_generic_pageout,
	.set_exe =	vop_generic_set_exe,
	.unset_exe =	vop_generic_unset_exe,
	.create =	ext2_create,
	.unlink =	ext2_unlink,
	.getdents =	ext2_getdents,
	.namei =	ext2_namei,
	.rename = 	ext2_rename,
	.truncate =	ext2_truncate,
	.bmap =		ext2_bmap,
	.sync =		ext2_vn_sync,
	.readlink =	ext2_readlink,
};

ASSERT(sizeof(ext2_emptydir_t) == 24, "");

static uint8_t ext2_dent_type(uint8_t type) {
	switch(type) {
	case EXT2_FT_REG_FILE:
		return DT_REG;
	case EXT2_FT_DIR:
		return DT_DIR;
	case EXT2_FT_CHRDEV:
		return DT_CHR;
	case EXT2_FT_BLKDEV:
		return DT_BLK;
	case EXT2_FT_FIFO:
		return DT_FIFO;
	case EXT2_FT_SYMLINK:
		return DT_LNK;
	case EXT2_FT_UNKNOWN:
	default:
		return DT_UNKNOWN;
	}
}

static int ext2_vn_sync(vnode_t *node) {
	ext2_fs_t *fs = filesys_get_priv(node->fs);
	ext2_inode_t *inode = EXT2_VTOI(node);

	inode->i_uid = node->uid;
	inode->i_gid = node->gid;
	inode->i_mode = node->mode;
	inode->i_nlink = node->nlink;
	inode->i_blocks = node->blocks;
	inode->i_atime = node->atime.tv_sec;
	inode->i_ctime = node->ctime.tv_sec;
	inode->i_mtime = node->mtime.tv_sec;

	if(node->nlink == 0) {
		struct timespec ts;
		realtime(&ts);
		inode->i_dtime = ts.tv_sec;
	} else {
		inode->i_dtime = 0;
	}

	inode->i_size = node->size & 0xFFFFFFFF;
	if(fs->super.s_rev_level > EXT2_GOOD_OLD_REV) {
		inode->i_dir_acl = node->size >> 32;
	}

	/*
	 * Write the inode to disk.
	 */
	return ext2_set_inode(fs, node->ino, inode);
}

static int ext2_truncate(vnode_t *node, vnode_size_t length) {
	ext2_fs_t *fs = filesys_get_priv(node->fs);
	return ext2_inode_truncate(fs, node, length);
}

static int ext2_bmap(vnode_t *node, bool alloc, blkno_t lbn, blkno_t *pbn) {
	ext2_fs_t *fs = filesys_get_priv(node->fs);
	return ext2_inode_bmap(fs, node, alloc, lbn, pbn);
}

static ssize_t ext2_getdents(vnode_t *node, uio_t *uio) {
	ext2_fs_t *fs = filesys_get_priv(node->fs);
	ext2_dent_t ext2_dent;
	struct kdirent dent;
	blkno_t lbn, pbn;
	vnode_size_t off;
	size_t done;
	int err;

	off = uio->off;
	done = 0;
	lbn = -1;
	pbn = -1;

	/* TODO stronger checking (user may provide any offset) */
	while(uio->size >= DIRENT_SZ && (err = ext2_getdent(fs, node, off, &lbn,
		&pbn, &ext2_dent)) != 1)
	{
		if(err < 0) {
			return err;
		}

		off += ext2_dent.rec_len;
		if(ext2_dent.inode == 0) {
			uio->off = off;
			continue;
		}

		dent.d_reclen = DIRENT_LEN(ext2_dent.name_len);
		if(uio->size < dent.d_reclen) {
			break;
		}

		if(fs->super.s_rev_level > EXT2_GOOD_OLD_REV) {
			dent.d_type = ext2_dent_type(ext2_dent.file_type);
		} else {
			dent.d_type = DT_UNKNOWN;
		}

		dent.d_ino = ext2_dent.inode;
		dent.d_off = off;

		/*
		 * The name buffer is one byte larger than needed in my
		 * ext2_dent_t structure, so it's possible to add the '\0'.
		 */
		ext2_dent.name[ext2_dent.name_len] = '\0';
		err = uiodirent(&dent, ext2_dent.name, ext2_dent.name_len, uio);
		if(err < 0) {
			return err;
		}

		done += dent.d_reclen;
		uio->off = off;
	}

	return done;
}

static int ext2_namei(vnode_t *node, const char *name, size_t namelen,
	vnamei_op_t op, ino_t *ino)
{
	ext2_fs_t *fs = filesys_get_priv(node->fs);
	vnamei_aux_t *aux = EXT2_VTONAMEI(node);
	ext2_dent_t *dent;
	blkno_t lbn, pbn;
	vnode_size_t off;
	int err;

	/*
	 * TODO it would be pretty cool if the block device cache
	 * interface was able to give us a buffer.
	 * -- easy!! Then no copying would be involved
	 *			 Could be also used for superblock!!!
	 */
	dent = kmalloc(sizeof(*dent), VM_WAIT);
	off = 0;
	lbn = -1;
	pbn = -1;

	/*
	 * Don't override the information from the previous lookup, needed
	 * for rename.
	 */
	if(op != VNAMEI_RENAME) {
		aux->prev_off = 0;
		aux->prev_size = 0;
	}

	aux->hole_blk = 0;

	while((err = ext2_getdent(fs, node, off, &lbn, &pbn, dent)) != 1) {
		size_t tmp;

		if(err < 0) {
			goto out;
		} else if(dent->inode != 0 && dent->name_len == namelen &&
			!memcmp(dent->name, name, namelen))
		{
			*ino = dent->inode;

			if(op == VNAMEI_RENAME) {
				/*
				 * On rename, the target directory and the
				 * source directory may be equal, so don't store
				 * this informatiom in namei.off and namei.blkno
				 * since this would override the result from the
				 * last VNAMEI_LOOKUP.
				 */
				aux->hole_off = off;
				aux->hole_blk = pbn;
			} else if(op != VNAMEI_LOOKUP) {
				aux->off = off;
				aux->size = dent->rec_len;
				aux->blkno = pbn;
			}

			err = 0;
			goto out;
		}

		/*
		 * Search for a hole (used in a subsequent call to
		 * ext2_add_dent) TODO VNAMEI_LINK.
		 */
		if(op == VNAMEI_CREATE || op == VNAMEI_RENAME) {
			if(dent->inode == 0) {
				tmp = dent->rec_len;
			} else {
				tmp = EXT2_DENT_HOLE(dent);
			}

			/*
			 * Use the first hole which can fit a new directory
			 * with this name.
			 */
			if(tmp > EXT2_RECLEN(namelen) && aux->hole_blk == 0) {
				aux->hole_off = off;
				aux->hole_blk = pbn;
			}
		}

		/*
		 * Don't override the information from the previous lookup,
		 * needed for rename.
		 */
		if(op != VNAMEI_LOOKUP && op != VNAMEI_RENAME) {
			aux->prev_off = off;
			aux->prev_size = dent->rec_len;
		}

		off += dent->rec_len;
	}

	err = -ENOENT;
out:
	kfree(dent);
	return err;
}

static int ext2_create(vnode_t *node, const char *name, size_t namelen,
	vnode_create_args_t *args, vnode_t **childp)
{
	ext2_fs_t *fs = filesys_get_priv(node->fs);
	ext2_inode_t *inode;
	blkno_t blk = 0;
	vnode_t *child;
	uint32_t ino;
	int err;

	if(namelen > EXT2_NAMELEN) {
		return -ENAMETOOLONG;
	}

	err = ext2_ialloc(fs, &ino);
	if(err) {
		return err;
	}

	/*
	 * Allocate a new vnode.
	 */
	child = ext2_valloc(node->fs);
	inode = child->priv;

	child->ino = ino;
	child->uid = args->uid;
	child->gid = args->gid;
	child->mode = args->mode;
	child->rdev = 0;
	child->nlink = 1;
	child->blocks = 0;
	memset(inode, 0x00, sizeof(ext2_inode_t));

	if(S_ISDIR(args->mode)) {
		ext2_emptydir_t dir;

		vnode_init_size(child, fs->blksz);
		child->nlink = 2;

		dir.dent[0].inode = ino;
		dir.dent[0].rec_len = 12;
		dir.dent[0].name_len = 1;
		dir.dent[0].file_type = EXT2_FT_DIR;
		dir.dent[0].name[0] = '.';
		dir.dent[0].name[1] = '\0';
		dir.dent[0].name[2] = '\0';
		dir.dent[0].name[3] = '\0';
		dir.dent[1].inode = node->ino;
		dir.dent[1].rec_len = fs->blksz - 12;
		dir.dent[1].name_len = 2;
		dir.dent[1].file_type = EXT2_FT_DIR;
		dir.dent[1].name[0] = '.';
		dir.dent[1].name[1] = '.';
		dir.dent[1].name[2] = '\0';
		dir.dent[1].name[3] = '\0';

		synchronized(&VNTOVM(child)->lock) {
			/*
			 * bmap does not sync the node. This happens below.
			 */
			err = ext2_inode_bmap(fs, child, true, 0, &blk);
		}
		if(err) {
			goto error;
		}

		/*
		 * Write "." and ".." dents into block.
		 */
		err = bwrite(fs->dev, EXT2_BOFF(fs, blk, 0), sizeof(dir), &dir);
		if(err) {
			goto error2;
		}
	} else if(S_ISLNK(args->mode)) {
		vnode_init_size(child, args->symlink.len);
		if(args->symlink.len <= EXT2_SYMLINK_INLINE) {
			memcpy((void *)inode->i_block, args->symlink.name,
				args->symlink.len);
		} else {
			kpanic("[ext2] TODO");
		}
	} else if(S_ISCHR(args->mode) || S_ISBLK(args->mode)){
		return -ENOTSUP;
	}

	wrlocked(&child->statlock) {
		vnode_settime(child, VN_CURTIME, VN_ATIME | VN_MTIME |
			VN_CTIME);
		vnode_dirty(child);
	}

	err = vnode_sync(child);
	if(err) {
		goto error2;
	}

	/*
	 * Try adding directory entry.
	 */
	err = ext2_add_dent(fs, node, name, namelen, child);
	if(err) {
		goto error2;
	}

	if(VN_ISDIR(child)) {
		ext2_ino_bgd(fs, ino)->bg_used_dirs_count++;
	}

	/*
	 * Add a link to the directory, because the child directory
	 * contains a  ".." entry.
	 */
	wrlocked(&node->statlock) {
		if(VN_ISDIR(child)) {
			vnode_nlink_inc(node);
		}

		vnode_settime(node, VN_CURTIME, VN_CTIME | VN_MTIME);
		vnode_dirty(node);
	}

	vnode_sync(node); /* TODO error is ignored */

	/*
	 * Insert new node into vfs cache. fs->lock would not be needed, but
	 * vnode_cache_add has got a sync_assert.
	 */
	synchronized(&child->fs->lock) {
		vnode_cache_add(child);
	}

	*childp = child;

	return 0;

error2:
	if(S_ISDIR(args->mode)) {
		ext2_bfree(fs, blk);
	}
error:
	ext2_ifree(fs, ino);
	ext2_vfree(child);
	return err;
}

static int ext2_unlink(vnode_t *dir, const char *name, size_t namelen,
	int flags, vnode_t *child)
{
	ext2_fs_t *fs = filesys_get_priv(dir->fs);
	int err;

	/*
	 * _name_ and _namelen_ are not needed because the last call to
	 * ext2_namei saved all the required information for this op.
	 */
	(void) name;
	(void) namelen;

#if 0
	kprintf("[ext2] unlink(%s): %lld %d\n", name, child->ino, child->ref);
#endif

	if(flags & AT_REMOVEDIR) {
		int res;

		/*
		 * Check if the directory is empty.
		 */
		res = ext2_dirempty(fs, child);
		if(res == 0) {
			return -ENOTEMPTY;
		} else if(res < 0) {
			return res;
		}
	}

	/*
	 * Remove the directory entry looked up by the last call to ext2_namei.
	 */
	err = ext2_rmdent(fs, dir);
	if(err) {
		return err;
	}

	wrlocked(&child->statlock) {
		vnode_nlink_dec(child);
		vnode_settime(child, VN_CURTIME, VN_CTIME);

		if(flags & AT_REMOVEDIR) {
			vnode_nlink_set(child, 0);	
		}

		vnode_dirty(child);
	}

	if(flags & AT_REMOVEDIR) {
		wrlock_scope(&dir->statlock);
		vnode_nlink_dec(dir);
		vnode_dirty(dir);
	} else {
		err = 0;
	}

	if(!err) {
		err = vnode_sync(dir);
		vnode_sync(child);
	}

	return err;
}

static int ext2_dirent_retarget(ext2_fs_t *fs, vnode_t *dir, vnode_t *new) {
	vnamei_aux_t *namei = EXT2_VTONAMEI(dir);
	uint32_t ino = new->ino;
	size_t off;

	off = (namei->hole_off & (fs->blksz - 1)) +
		offsetof(ext2_dent_t, inode);

	/*
	 * In VNAMEI_LOOKUP ext2_namei stores the relevant information in
	 * hole_blk and hole_size if the rename-target was present.
	 */
	return bwrite(fs->dev, EXT2_BOFF(fs, namei->hole_blk, off), sizeof(ino),
		&ino);
}

static int ext2_dirent_reparent(ext2_fs_t *fs, vnode_t *dir, vnode_t *parent) {
	uint32_t ino = parent->ino;
	blkno_t blk;
	int err;

	/*
	 * Get the first number of the first block, which contains the ".."
	 * entry.
	 */
	err = ext2_inode_bmap(fs, dir, false, 0, &blk);
	if(err) {
		return err;
	} else if(blk == 0) {
		kpanic("[ext2] invalid directory");
	}

	return bwrite(fs->dev, EXT2_BOFF(fs, blk, EXT2_DOT_DOT_IDX + 
		offsetof(ext2_dent_t, inode)), sizeof(ino), &ino);
}

extern void ext2_checkdir(const char *chr, vnode_t *node);

static int ext2_rename(vnode_t *olddir, const char *oldname, size_t oldlen,
	vnode_t *old, vnode_t *newdir, const char *newname, size_t newlen,
	vnode_t *new)
{
	ext2_fs_t *fs = filesys_get_priv(olddir->fs);	
	int err;

	(void) oldname;
	(void) oldlen;

	if(new) {
		if(VN_ISDIR(new) && !ext2_dirempty(fs, new)) {
			return -ENOTEMPTY;
		}

		/*
		 * Point the dirent of _new_ to _old_.
		 */
		err = ext2_dirent_retarget(fs, newdir, old);
		if(err) {
			return err;
		}

		wrlocked(&new->statlock) {
			if(VN_ISDIR(new)) {
				vnode_nlink_set(new, 0);
			} else {
				vnode_nlink_dec(new);
			}

			vnode_dirty(new);
		}

		vnode_sync(new);
	} else {
		/*
		 * Add a new directory entry for _old_.
		 */
		err = ext2_add_dent(fs, newdir, newname, newlen, old);
		if(err) {
			return err;
		}
	}

	if(VN_ISDIR(old)) {
		/*
		 * Repoint '..' of the old directory if necessary.
		 */
		if(olddir != newdir) {
			err = ext2_dirent_reparent(fs, old, newdir);
			if(err) {
				assert(err == -EIO);

				/*
				 * The directory is now present in 2 directories
				 * => bump link count
				 */
				wrlocked(&old->statlock) {
					vnode_nlink_inc(old);
					vnode_dirty(old);
				}
				vnode_sync(old);

				return err;
			}

			/*
			 * The ".." entry of this node pointed to _olddir_
			 * => remove that link
			 */
			wrlocked(&olddir->statlock) {
				vnode_nlink_dec(olddir);
				vnode_dirty(olddir);
			}
			vnode_sync(olddir);

			if(new == NULL) {
				/*
				 * If _new_ was present, its ".." entry pointed
				 * to _newdir_ and since _newdir_'s nlink is not
				 * decremented by the code above, nlink is
				 * correct (".." of _new_ is removed and ".." of
				 * _old_ now points to _newdir_). If _new_ was
				 * not present, add the missing link due to ".."
				 * of _old_.
				 */
				wrlocked(&newdir->statlock) {
					vnode_nlink_inc(newdir);
					vnode_dirty(newdir);
				}
				vnode_sync(newdir);
			}
		} else if(new) {
			/*
			 * If both _new_ and _old_ are in the same directory,
			 * both ".." were pointing to the same directory. After
			 * the rename only _old_'s ".." is still pointing to the
			 * directory => remove one link
			 */
			wrlocked(&newdir->statlock) {
				vnode_nlink_dec(newdir);
				vnode_dirty(newdir);
			}
			vnode_sync(newdir);
		}
	}

	/*
	 * Remove the old directory entry.
	 */
	return ext2_rmdent(fs, olddir);
}

static ssize_t ext2_readlink(vnode_t *node, uio_t *uio) {
	ext2_inode_t *inode = EXT2_VTOI(node);
	ssize_t res;

	assert(uio->size > 0);

	/*
	 * -1 because the uio needs a trailing '\0'.
	 */
	uio->size--;
	if(uio->size == 0) {
		return 0;
	}

	if(node->size <= EXT2_SYMLINK_INLINE) {
		void *buf = (void *)inode->i_block;
		res = uiomove(buf, node->size, uio);
	} else {
		res = vop_generic_rdwr(node, uio);
	}

	if(res > 0) {
		int err;

		/*
		 * Add the '\0'.
		 */
		uio->size++;
		err = uiomemset(uio, 1, 0x0);
		if(err) {
			res = err;
		} else {
			res += 1;
		}
	}

	return res;
}

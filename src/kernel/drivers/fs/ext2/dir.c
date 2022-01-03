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
#include <ext2/ext2.h>
#include <lib/string.h>

static uint8_t ext2_dt_from_mode(mode_t mode) {
	assert((mode & ~S_IFMT) == 0);
	switch(mode) {
	case S_IFDIR:
		return EXT2_FT_DIR;
	case S_IFREG:
		return EXT2_FT_REG_FILE;
	case S_IFCHR:
		return EXT2_FT_CHRDEV;
	case S_IFBLK:
		return EXT2_FT_BLKDEV;
	case S_IFIFO:
		return EXT2_FT_FIFO;
	case S_IFLNK:
		return EXT2_FT_SYMLINK;
	default:
		return EXT2_FT_UNKNOWN;
	}
}

static bool ext2_bad_dent(ext2_fs_t *fs, ext2_dent_t *dent, size_t maxsz) {
	const char *err;

	if(dent->rec_len < EXT2_DENT_SZ) {
		err = "dent too small";
	} else if(dent->rec_len > maxsz) {
		err = "dent too big";
	} else if(dent->inode > fs->super.s_inodes_count) {
		err = "inode out bounds";
	} else if(dent->name_len > dent->rec_len - EXT2_DENT_SZ) {
		err = "name_len too large";
	} else if(!ALIGNED(dent->rec_len, 4)) {
		err = "rec_len not aligned";
	} else {
		return false;
	}

	dent->name[min(EXT2_NAMELEN, dent->name_len)] = '\0';
	kprintf("[ext2] borken dirent: %s: %s\n", dent->name, err);
	return true;
}

int ext2_getdent(ext2_fs_t *fs, vnode_t *node, vnode_size_t off, blkno_t *lbn,
	blkno_t *pbn, ext2_dent_t *dent)
{
	size_t maxsz, blkoff;
	blkno_t num;
	int err;

	VN_ASSERT_LOCK_WR(node);
	if(off >= node->size) {
		return 1;
	}

	num = off >> fs->blkshift;
	blkoff = off & (fs->blksz - 1);

	/*
	 * A directory entry has to be inside one block.
	 */
	maxsz = min(fs->blksz - blkoff, node->size - off);
	if(maxsz < EXT2_DENT_SZ) {
		goto broken;
	}

	/*
	 * Get the physical block number if needed.
	 */
	if(num != *lbn) {
		*lbn = num;

		err = ext2_inode_bmap(fs, node, false, *lbn, pbn);
		if(err) {
			return err;
		}

		/*
		 * No holes for directories.
		 */
		if(pbn == 0) {
			kpanic("error directory has a hole: ino: %lld",
				node->ino);
		}
	}

	/*
	 * Read the dirent structure. Remember that my ext2_dent_t structure
	 * is one byte bigger than in reality.
	 */
	err = bread(fs->dev, (*pbn << fs->blkshift) + blkoff,
		min(maxsz, sizeof(ext2_dent_t) - 1), dent);
	if(err) {
		return err;
	}

	/*
	 * Check for sanity of dent. This also avoids infinite loops (i.e.
	 * rec_len == 0).
	 */
	if(!ext2_bad_dent(fs, dent, maxsz)) {
    	return 0;
	}

broken:
	kpanic("[ext2] broken directory entry: ino: %lld, off: %d",
			node->ino, blkoff);
}

int ext2_dirempty(ext2_fs_t *fs, vnode_t *dir) {
	ext2_dent_t dent;
	blkno_t pbn, lbn;
	off_t off;
	int res;

	pbn = lbn = -1;
	off = 0;

	while((res = ext2_getdent(fs, dir, off, &lbn, &pbn, &dent)) != 1) {
		if(res < 0) {
			return res;
		}

		if(dent.inode != 0) {
			if(dent.name_len > 2 || dent.name[0] != '.') {
				return 0;
			} else if(dent.name_len == 2 && dent.name[1] != '.') {
				return 0;
			}
		}

		/*
		 * "." or ".."
		 */
		off += dent.rec_len;
		continue;
	}

	return 1;
}

int ext2_add_dent(ext2_fs_t *fs, vnode_t *dir, const char *name, size_t namelen,
	vnode_t *node)
{
	vnamei_aux_t *namei = EXT2_VTONAMEI(dir);
	size_t rec_len, size = 0, off;
	ext2_dent_t *dent = NULL;
	blkno_t blk;
	void *buf;
	int err;

	VN_ASSERT_LOCK_WR(dir);
	rec_len = EXT2_RECLEN(namelen);

	/*
	 * Allocate a temporary buffer with space for 2 directory entries.
	 */
	dent = buf = kmalloc(sizeof(ext2_dent_t) + rec_len, VM_WAIT);
	if(namei->hole_blk != 0) {
		/*
		 * Use ther information gathered during last call to
		 * ext2_finddir.
		 */
		off = namei->hole_off & (fs->blksz - 1);
		blk = namei->hole_blk;

		/*
		 * Read the old directory entry at hole_off.
		 */
		err = bread(fs->dev, EXT2_BOFF(fs, blk, off), min(EXT2_DENT_MAX,
			fs->blksz - off), dent);
		if(err) {
			goto out;
		}

		if(dent->inode == 0) {
			/*
			 * If the inode is zero, this dirent can be used for
			 * the new dirent.
			 */
			size = rec_len;
			rec_len = dent->rec_len;
		} else {
			size_t old_len = dent->rec_len;

			/*
			 * Use the redundant memory of the old dirent for the
			 * new dirent, by shrinking the old dirent to its
			 * minimum size and adding a new dirent right after
			 * that.
			 */
			dent->rec_len = EXT2_RECLEN(dent->name_len);

			/*
			 * This is the size to write to disk.
			 */
			size = dent->rec_len + rec_len;

			/*
			s * Calculate the real rec_len of the new dirent.
			 */
			rec_len = old_len - dent->rec_len;

			/*
			 * Needed for a rename op to not override the new
			 * directory entry, if it is right after the old
			 * directory entry being removed.
			 */
			if(namei->hole_off == namei->off) {
				namei->size = dent->rec_len;
			} else if(namei->hole_off + old_len == namei->off) {
				namei->prev_size = rec_len;
				namei->prev_off += dent->rec_len;
			}

			dent = buf + dent->rec_len;
		}
	} else {
		vnode_size_t length = ALIGN(node->size, fs->blksz);

		/*
		 * Allocate a new block at the end of the node.
		 */
		err = ext2_inode_bmap(fs, dir, true, length >> fs->blkshift,
			&blk);
		if(err) {
			goto out;
		}

		vnode_set_size(dir, length + fs->blksz);
		err = vnode_sync(dir);
		if(err) {
			return err;
		}

		/*
		 * The size to write to disk is equal to the minimum rec_len of
		 * the new dirent.
		 */
		size = rec_len;

		/*
		 * the new dirent contains the whole fs-block.
		 */
		off = 0;
		rec_len = fs->blksz;
	}

	/*
	 * Configure the new directory entry.
	 */
	dent->inode = node->ino;
	dent->rec_len = rec_len;
	dent->name_len = namelen;
	/* TODO EXT2-REVISION DEPENDEND */
	dent->file_type = ext2_dt_from_mode(VN_TYPE(node));
	memcpy(dent->name, name, namelen);

	err = bwrite(fs->dev, EXT2_BOFF(fs, blk, off), size, buf);

out:
	kfree(buf);
	return err;
}

int ext2_rmdent(ext2_fs_t *fs, vnode_t *node) {
	vnamei_aux_t *namei = EXT2_VTONAMEI(node);

	/*
	 * Since the last call to finddir saved a lot of information, this
	 * function is pretty straightforward.
	 */
	if(ALIGNED(namei->off, fs->blksz)) {
		if(namei->off + namei->size == node->size) {
			return ext2_inode_truncate(fs, node, node->size -
				fs->blksz);
		} else {
			uint32_t ino = 0;

			/*
			 * TODO maybe consider collapsing with the next entry.
			 */
			return bwrite(fs->dev, EXT2_BOFF(fs, namei->blkno,
				offsetof(ext2_dent_t, inode)), sizeof(ino),
				&ino);
		}
	} else {
		uint16_t rec_len;
		off_t off;

		rec_len = namei->prev_size + namei->size;
		off = EXT2_BOFF(fs, namei->blkno, (namei->prev_off &
			(fs->blksz - 1)) + offsetof(ext2_dent_t, rec_len));

		return bwrite(fs->dev, off, sizeof(rec_len), &rec_len);
	}
}

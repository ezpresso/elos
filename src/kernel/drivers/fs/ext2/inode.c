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
#include <vm/object.h>
#include <vm/vm.h> /* vm_zero_map TODO rmv */
#include <ext2/ext2.h>

static int ext2_get_set_inode(ext2_fs_t *fs, uint32_t ino, ext2_inode_t *inode,
	blk_rtype_t type)
{
	uint32_t blk, bgd_id;
	off_t off;

	ino--;
	bgd_id = ino / fs->super.s_inodes_per_group;

	ino %= fs->super.s_inodes_per_group;
	blk = fs->bgds[bgd_id].bg_inode_table;
	blk += ino / fs->ino_per_blk;
	off = (ino % fs->ino_per_blk) * fs->ino_sz;
	return bio(fs->dev, type, EXT2_BOFF(fs, blk, off), sizeof(*inode),
		inode);
}

int ext2_get_inode(ext2_fs_t *fs, uint32_t ino, ext2_inode_t *inode) {
	return ext2_get_set_inode(fs, ino, inode, BLK_RD);
}

int ext2_set_inode(ext2_fs_t *fs, uint32_t ino, ext2_inode_t *inode) {
	return ext2_get_set_inode(fs, ino, inode, BLK_WR);
}

static void ext2_inc_blockcnt(ext2_fs_t *fs, vnode_t *node) {
	/*
	 * Update the number of blocks the node owns. Remember that this
	 * value counts 512 byte blocks instead of filesystem blocks.
	 */
	vnode_set_blocks(node, node->blocks + EXT2_IBLKS(fs));
	vnode_dirty(node);
}

static int ext2_read_blkaddr(ext2_fs_t *fs, vnode_t *node, uint32_t block,
	size_t index, bool alloc, uint32_t *blkp)
{
	off_t off = EXT2_BOFF(fs, block, index << 2);
	int err;

	err = bread(fs->dev, off, 4, blkp);
	if(err) {
		return err;
	}

	if(*blkp == 0 && alloc) {
		err = ext2_balloc(fs, node, blkp);
		if(err) {
			return err;
		}

		/*
		 * Have to clear that block before using it.
		 */
		err = ext2_bclr(fs, *blkp);
		if(err == 0) {
			err = bwrite(fs->dev, off, 4, blkp);
		}

		if(err) {
			ext2_bfree(fs, *blkp);
		} else {
			wrlock_scope(&node->statlock);
			ext2_inc_blockcnt(fs, node);
		}
	}

	return err;
}

int ext2_inode_bmap(ext2_fs_t *fs, vnode_t *node, bool alloc, blkno_t lbn,
	blkno_t *pbn)
{
	ext2_inode_t *inode = vnode_priv(node);
	uint32_t block, idx;
	int err = 0;

	if(lbn < EXT2_NDIR_BLOCKS) {
		idx = lbn;
	} else {
		lbn -= EXT2_NDIR_BLOCKS;
		if(lbn < fs->addr_per_blk) {
			idx = EXT2_IND_BLOCK;
		} else if(lbn < (1U << fs->log_naddr_sq)) {
			idx = EXT2_DIND_BLOCK;
			lbn -= fs->addr_per_blk;
		} else {
			idx = EXT2_TIND_BLOCK;
			lbn -= (fs->addr_per_blk + (1U << fs->log_naddr_sq));
		}
	}

	block = inode->i_block[idx];
	if(block == 0 && alloc) {
		err = ext2_balloc(fs, node, &block);
		if(err) {
			return err;
		} else if(idx >= EXT2_NDIR_BLOCKS) {
			err = ext2_bclr(fs, block);
			if(err) {
				ext2_bfree(fs, block);
				return err;
			}
		}

		wrlocked(&node->statlock) {
			inode->i_block[idx] = block;
			ext2_inc_blockcnt(fs, node);
		}
	}

	/*
	 * Direct blocks are easy.
	 */
	if(idx < EXT2_NDIR_BLOCKS) {
		*pbn = block;
		return 0;
	}

	switch(idx) {
	case EXT2_TIND_BLOCK:
		err = ext2_read_blkaddr(fs, node, block,
			lbn >> fs->log_naddr_sq, alloc, &block);
		if(err || block == 0) {
			*pbn = block;
			return err;
		}

		lbn &= (1ULL << fs->log_naddr_sq) - 1;
	/* FALLTHROUGH */
	case EXT2_DIND_BLOCK:
		err = ext2_read_blkaddr(fs, node, block, lbn >> fs->log_naddr,
			alloc, &block);
		if(err || block == 0) {
			*pbn = block;
			return err;
		}

		lbn &= (1ULL << fs->log_naddr) - 1;
	/* FALLTHROUGH */
	case EXT2_IND_BLOCK:
		err = ext2_read_blkaddr(fs, node, block, lbn, alloc, &block);
	}

	*pbn = block;
	return err;
}

static int ext2_trunc_indir(ext2_fs_t *fs, uint8_t indir, uint32_t block,
	uint32_t start, blkcnt_t *count)
{
	uint32_t *buf, lg, off;
	int err, allerr = 0;

	assert(indir > 0);
	if(block == 0) {
		return 0;
	}

	buf = kmalloc(fs->blksz, VM_WAIT);
	allerr = bread(fs->dev, EXT2_BOFF(fs, block, 0), fs->blksz, buf);
	if(allerr) {
		goto out;
	}

	lg = (indir - 1) * fs->log_naddr;
	off = start & ((1 << lg) - 1);
	start >>= lg;

	for(size_t i = start; i < fs->addr_per_blk; i++) {
		if(buf[i]) {
			if(indir == 1) {
				ext2_bfree(fs, buf[i]);
				*count += EXT2_IBLKS(fs);
			} else {
				err = ext2_trunc_indir(fs, indir - 1, buf[i],
					off, count);
				if(err) {
					allerr = err;
				}
			}

			buf[i] = 0;
		}

		off = 0;
	}

	/*
	 * Free the indirect block.
	 */
	if(start == 0) {
		ext2_bfree(fs, block);
		*count += EXT2_IBLKS(fs);
	} else {
		/*
		 * Write back the block to the device if the block still
		 * contains valid addresses.
		 */
		err = bwrite(fs->dev, EXT2_BOFF(fs, block, 0), fs->blksz, buf);
		if(err) {
			allerr = err;
		}
	}

out:
	kfree(buf);
	return allerr;
}

int ext2_inode_truncate(ext2_fs_t *fs, vnode_t *node, vnode_size_t length) {
	const vnode_size_t oldsz = node->size;
	ext2_inode_t *inode = vnode_priv(node);
	uint32_t flbn, old[EXT2_N_BLOCKS];
	int err, allerr = 0;
	blkcnt_t count = 0;
	size_t i;

	vnode_set_size(node, length);
	if(length > oldsz) {
		size_t off;

		if(length > fs->maxlen) {
			return -EFBIG;
		}

		/**
		 * TODO shouldn't one consider using the vnode_page stuff here
		 * to avoid errors?
		 */
		off = oldsz & (fs->blksz - 1);
		if(off) {
			blkno_t pbn;

			err = ext2_inode_bmap(fs, node, false,
				oldsz >> fs->blkshift, &pbn);
			if(!err && pbn != 0) {
				/*
				 * When increasing the size make sure that
				 * the rest of the former last block of the file
				 * does not contain any junk.
				 */
				err = bwrite(fs->dev, EXT2_BOFF(fs, pbn, off),
					fs->blksz - off, vm_zero_map);
			}

			if(err) {
				return err;
			}
		}

		goto out;
	}

	/*
	 * Calculate the index of the first block to free.
	 */
	wrlocked(&node->statlock) {
		flbn = (ALIGN(length, fs->blksz) >> fs->blkshift);
		for(i = 0; i < EXT2_N_BLOCKS; i++) {
			old[i] = inode->i_block[i];
			if(i < EXT2_NDIR_BLOCKS && i >= flbn) {
				inode->i_block[i] = 0;
			}
		}

		if(flbn <= EXT2_NDIR_BLOCKS + fs->addr_per_blk +
			fs->addr_per_blk_sq)
		{
			inode->i_block[EXT2_TIND_BLOCK] = 0;
		}

		if(flbn <= EXT2_NDIR_BLOCKS + fs->addr_per_blk) {
			inode->i_block[EXT2_DIND_BLOCK] = 0;
		}

		if(flbn <= EXT2_NDIR_BLOCKS) {
			inode->i_block[EXT2_IND_BLOCK] = 0;
		}

		vnode_dirty(node);
	}

	/*
	 * Try syncing the node. The error value returned is ignored since the
	 * sync is tried below another time.
	 */
	vnode_sync(node);

	/*
	 * Free direct blocks.
	 */
	for(i = 0; i < EXT2_NDIR_BLOCKS; i++) {
		if(i >= flbn && old[i]) {
			ext2_bfree(fs, old[i]);
			count += EXT2_IBLKS(fs);
		}
	}

	/*
	 * Handle single indirect blocks.
	 * Have to cast to signed integer because the result of
	 * "flbn - EXT2_NDIR_BLOCKS" may be negative.
	 */
	flbn = max((int64_t)flbn - EXT2_NDIR_BLOCKS, 0);
	err = ext2_trunc_indir(fs, 1, old[EXT2_IND_BLOCK], flbn, &count);
	if(err) {
		allerr = err;
	}

	/*
	 * Handle double indirect blocks.
	 */
	flbn = max((int64_t)flbn - fs->addr_per_blk, 0);
	err = ext2_trunc_indir(fs, 2, old[EXT2_DIND_BLOCK], flbn, &count);
	if(err) {
		allerr = err;
	}

	/*
	 * Handle triple indirect blocks.
	 */
	flbn = max((int64_t)flbn - fs->addr_per_blk_sq, 0);
	err = ext2_trunc_indir(fs, 3, old[EXT2_TIND_BLOCK], flbn, &count);
	if(err) {
		allerr = err;
	}

out:
	wrlocked(&node->statlock) {
		vnode_set_blocks(node, node->blocks - count);
		vnode_settime(node, VN_CURTIME, VN_CTIME | VN_MTIME);
		vnode_dirty(node);
	}

	err = vnode_sync(node);
	if(err) {
		allerr = err;
	}

	return allerr;
}

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

/**
 * @brief Allocate a bit from a bitmap block.
 */
static int ext2_bitmap_alloc(ext2_fs_t *fs, uint32_t bitmap, uint32_t min,
	uint32_t *buf, uint32_t *res)
{
	int err;

	sync_assert(&fs->alloc_lock);

	/*
	 * Read the bitmap block.
	 */
	err = bread(fs->dev, EXT2_BOFF(fs, bitmap, 0), fs->blksz,
		buf);
	if(err) {
		return err;
	}

	/*
	 * Try to find bits, which are not set in the bitmap.
	 */
	for(uint32_t i = 0; i < fs->super.s_blocks_per_group >> 5; i++) {
		uint32_t bit, idx, val = ~buf[i];
		if(i == 0 && min) {
			/*
			 * Clear the lowest bits, if a minimum was provided.
			 */
			val &= (1U << min) - 1;
		}

		/*
		 * Search for a free bit.
		 */
		bit = ffs(val);
		if(bit == 0) {
			/*
			 * Nothing found here...
			 */
			continue;
		}

		bit--; /* ffs returns bit+1 */
		idx = bit + (i << 5);
		if(idx > fs->super.s_blocks_per_group) {
			return -ENOSPC;
		}

		/*
		 * Set the bit in order to indicate that the block/inode is not
		 * free.
		 */
		bset(&buf[i], bit);
		*res = idx;

		/*
		 * Try to write the bitmap again
		 */
		return bwrite(fs->dev, EXT2_BOFF(fs, bitmap, i << 2), 4,
			&buf[i]);
	}

	return -ENOSPC;
}

static int ext2_bitmap_free(ext2_fs_t *fs, uint32_t bitmap, uint32_t idx) {
	uint8_t val, mask;
	uint32_t byte;
	int err;

	sync_assert(&fs->alloc_lock);
	byte = idx >> 3;
	mask = 1 << (idx & 0x7);

	/*
	 * Read the bitmap.
	 */
	err = bread(fs->dev, EXT2_BOFF(fs, bitmap, byte), 1, &val);
	if(err == 0) {
		/*
		 * Clear the bit and write the bitmap again.
		 */
		kassert(val & mask, "[ext2] freeing block which is already "
			"free");
		val &= ~mask;
		err = bwrite(fs->dev, EXT2_BOFF(fs, bitmap, byte), 1, &val);
	}

	return err;
}

int ext2_balloc(ext2_fs_t *fs, vnode_t *node, uint32_t *blkp) {
	uint32_t *buf, blk = 0, start;
	int err = 0;

	buf = kmalloc(fs->blksz, VM_WAIT);

	/*
	 * Prefer the block group of the inode to avoid unnecessary seeking.
	 */
	start = (node->ino - 1) / fs->super.s_inodes_per_group;

	sync_acquire(&fs->alloc_lock);
	if(fs->super.s_free_blocks_count == 0) {
		err = -ENOSPC;
		goto out;
	}

	for(size_t i = 0; i < fs->nbgd; i++) {
		uint32_t bgd_id = start + i;
		if(bgd_id >= fs->nbgd) {
			bgd_id -= fs->nbgd;
		}

		if(fs->bgds[bgd_id].bg_free_inodes_count == 0) {
			continue;
		}

		/*
		 * Try to allocate a block.
		 */
		err = ext2_bitmap_alloc(fs, fs->bgds[bgd_id].bg_block_bitmap, 0,
			buf, &blk);
		if(err == -ENOSPC) {
			/*
			 * bgd.bg_free_blocks_count was wrong for some reason...
			 */
			fs->bgds[bgd_id].bg_free_blocks_count = 0;

			/*
			 * Search in another block group.
			 */
			continue;
		} else if(err) {
			break;
		}

		/*
		 * Calculate the block number.
		 */
		blk += bgd_id * fs->super.s_blocks_per_group;
		fs->bgds[bgd_id].bg_free_blocks_count--;
		fs->super.s_free_blocks_count--;
		break;
	}

out:
	sync_release(&fs->alloc_lock);
	kfree(buf);
	*blkp = blk;

	return err;
}

void ext2_bfree(ext2_fs_t *fs, uint32_t blk) {
	uint32_t bgd, off;
	int err;

	bgd = blk / fs->super.s_blocks_per_group;
	off = blk - bgd * fs->super.s_blocks_per_group;

	sync_scope_acquire(&fs->alloc_lock);
	err = ext2_bitmap_free(fs, fs->bgds[bgd].bg_block_bitmap, off);
	if(err == 0) {
		fs->bgds[bgd].bg_free_blocks_count++;
		fs->super.s_free_blocks_count++;
	}
}

/**
 * TODO alloc the inode in the bgd of its directory
 */
int ext2_ialloc(ext2_fs_t *fs, uint32_t *res) {
	uint32_t *buf, ino = 0;
	int err = 0;

	buf = kmalloc(fs->blksz, VM_WAIT);

	sync_acquire(&fs->alloc_lock);
	if(fs->super.s_free_inodes_count == 0) {
		err = -ENOSPC;
		goto out;
	}

	/*
	 * Search through the block group descriptors to find any inode.
	 */
	for(size_t bgd_id = 0; bgd_id < fs->nbgd; bgd_id++) {
		uint32_t min = 0;

		if(fs->bgds[bgd_id].bg_free_inodes_count == 0) {
			continue;
		}

		if(bgd_id == 0 && fs->super.s_rev_level > EXT2_GOOD_OLD_REV) {
			min = fs->super.s_first_ino;
		}

		/*
		 * Try to allocate the inode.
		 */
		err = ext2_bitmap_alloc(fs, fs->bgds[bgd_id].bg_inode_bitmap,
			min, buf, &ino);
		if(err == -ENOSPC) {
			fs->bgds[bgd_id].bg_free_inodes_count = 0;
			continue;
		} else if(err) {
			break;
		}

		ino += bgd_id * fs->super.s_inodes_per_group + 1;
		fs->bgds[bgd_id].bg_free_inodes_count--;
		fs->super.s_free_inodes_count--;
		break;
	}

out:
	sync_release(&fs->alloc_lock);
	kfree(buf);
	*res = ino;
	return err;
}

void ext2_ifree(ext2_fs_t *fs, uint32_t ino) {
	uint32_t bgd, off;
	int err;

	ino--;
	bgd = ino / fs->super.s_inodes_per_group;
	off = ino - bgd * fs->super.s_inodes_per_group;

	sync_scope_acquire(&fs->alloc_lock);
	err = ext2_bitmap_free(fs, fs->bgds[bgd].bg_inode_bitmap, off);
	if(err == 0) {
		fs->bgds[bgd].bg_free_inodes_count++;
		fs->super.s_free_inodes_count++;
	}
}

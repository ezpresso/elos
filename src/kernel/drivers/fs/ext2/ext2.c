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
#include <vfs/fs.h>
#include <vfs/vcache.h>
#include <vm/slab.h>
#include <vm/malloc.h>
#include <vm/vm.h> /* vm_zero_map */
#include <ext2/ext2.h>
#include <sys/statfs.h>

static DEFINE_VM_SLAB(ext2_vnodes, sizeof(vnode_t) + sizeof(ext2_priv_t), 0);

vnode_t *ext2_valloc(filesys_t *fs) {
	ext2_fs_t *ext2 = filesys_get_priv(fs);
	vnode_t *node;

	node = vm_slab_alloc(&ext2_vnodes, VM_WAIT);
	vnode_init(node, fs, &ext2_vnops);
	node->priv = (void *)node + sizeof(*node);
	node->blksz_shift = ext2->blkshift;

	return node;
}

void ext2_vfree(vnode_t *node) {
	vnode_destroy(node);
	vm_slab_free(&ext2_vnodes, node);
}

int ext2_bclr(ext2_fs_t *fs, uint32_t blk) {
	assert(fs->blksz <= PAGE_SZ);
	return bwrite(fs->dev, EXT2_BOFF(fs, blk, 0), fs->blksz, vm_zero_map);
}

/* TODO this fn will have to be used static */ int ext2_fs_sync(ext2_fs_t *ext2) {
	int err, allerr = 0;

	/*
	 * Write the superblock to disk.
	 */
	allerr = bwrite(ext2->dev, EXT2_SBLOCK_OFF, sizeof(ext2_sblock_t),
		&ext2->super);

	/*
	 * Write the block group descriptors.
	 */
	for(size_t i = 0; i < ext2->bgd_blks; i++) {
		uint32_t num, blk = ext2->super.s_first_data_block + 1 + i;
		num = min(ext2->bgd_per_blk, ext2->nbgd -
			i *ext2->bgd_per_blk);
		err = bwrite(ext2->dev, EXT2_BOFF(ext2, blk, 0),
			num << EXT2_BGD_LOG_SZ,
			&ext2->bgds[i * ext2->bgd_per_blk]);
		if(err) {
			allerr = err;
		}
	}

	return allerr;
}

static int ext2_vget(filesys_t *fs, ino_t ino, vnode_t **out) {
	ext2_fs_t *ext2 = filesys_get_priv(fs);
	vnode_size_t length;
	ext2_inode_t *inode;
	vnode_t *node;
	int err;

	node = vnode_cache_lookup(fs, ino);
	if(node) {
		*out = node;
		return 0;
	}

	node = ext2_valloc(fs);
	inode = EXT2_VTOI(node);

	/*
	 * Read the node from the disk.
	 */
	err = ext2_get_inode(ext2, ino, inode);
	if(err) {
		return err;
	}

	node->ino = ino;
	node->uid = inode->i_uid;
	node->gid = inode->i_gid;
	node->mode = inode->i_mode;
	node->rdev = 0; /* TODO */
	node->nlink = inode->i_nlink;
	node->blocks = inode->i_blocks;
	node->atime.tv_sec = inode->i_atime;
	node->atime.tv_nsec = 0;
	node->ctime.tv_sec = inode->i_ctime;
	node->ctime.tv_nsec = 0;
	node->mtime.tv_sec = inode->i_mtime;
	node->mtime.tv_nsec = 0;

	length = inode->i_size;
	if(ext2->super.s_rev_level > EXT2_GOOD_OLD_REV && !VN_ISDIR(node)) {
		length |= (vnode_size_t)inode->i_dir_acl << 32;
	}

	vnode_init_size(node, length);

	/*
	 * insert new vnode into cache.
	 */
	vnode_cache_add(node);
	*out = node;

	return 0;
}

static void ext2_vput(filesys_t *fs, vnode_t *node) {
	ext2_fs_t *ext2 = filesys_get_priv(fs);

	if(node->nlink == 0) {
#if 0
		kprintf("[ext2] free: %lld\n", node->ino);
#endif

		/*
		 * Make sure no _sync_assert_s fail.
		 */
		wrlocked(&node->lock) {
			ext2_inode_truncate(ext2, node, 0);
		}

		ext2_ifree(ext2, node->ino);

		/*
		 * Directories have been truncated to zero in unlink.
		 */
		if(VN_ISDIR(node)) {
			ext2_ino_bgd(ext2, node->ino)->bg_used_dirs_count--;
		}
	}

	ext2_vfree(node);
}

static void ext2_statfs(filesys_t *fs, struct statfs *stat) {
	ext2_fs_t *ext2 = filesys_get_priv(fs);
	/* TODO */
	stat->f_type = EXT2_SUPER_MAGIC;
	stat->f_bsize = ext2->blksz; /* ??? */
	stat->f_blocks = ext2->super.s_blocks_count;
	stat->f_bfree = ext2->super.s_free_blocks_count;
	/* TODO s_r_blocks_count*/
	stat->f_bavail = ext2->super.s_free_blocks_count;
	stat->f_files = ext2->super.s_inodes_count;
	stat->f_ffree = ext2->super.s_free_inodes_count;
	/* TODO */
	stat->f_fsid.val[0] = 0;
	stat->f_fsid.val[1] = 0;
	stat->f_namelen = EXT2_NAMELEN;
	stat->f_frsize = 0;
	stat->f_flags = 0;
}

static bool ext2_parse_super(ext2_fs_t *ext2, ext2_sblock_t *sblock) {
	if(sblock->s_magic != EXT2_SUPER_MAGIC) {
		ext2_dbg("wrong magic: 0x%x\n", sblock->s_magic);
		return false;
	} else if(sblock->s_rev_level != EXT2_GOOD_OLD_REV &&
		sblock->s_rev_level != EXT2_DYNAMIC_REV)
	{
		ext2_dbg("invalid revision: 0x%x\n", sblock->s_rev_level);
		return false;
	} else if(sblock->s_state != EXT2_VALID_FS &&
			sblock->s_state != EXT2_ERROR_FS)
	{
		ext2_dbg("invalid state: 0x%x\n", sblock->s_state);
		return false;
	} else if((sblock->s_feature_incompat &
			~EXT2_FEATURE_INCOMPAT_FILETYPE) != 0)
	{
		ext2_dbg("unsupported features: 0x%x\n",
			sblock->s_feature_incompat);
		return false;
	}

	ext2->blkshift = EXT2_MIN_BLKSZ_LOG + sblock->s_log_block_size;
	ext2->blksz = 1 << ext2->blkshift;

	/*
	 * The inode bitmap has to fit into a single block -> there
	 * is a maximum amount of inodes that can be in a block group.
	 */
	if(sblock->s_inodes_per_group > (ext2->blksz << 3)) {
		ext2_dbg("s_inodes_per_group invalid: %d\n",
			sblock->s_inodes_per_group);
	}

	ext2->log_naddr = ext2->blkshift - EXT2_ADDR_LOG_SZ;
	ext2->log_naddr_sq = ext2->log_naddr + ext2->log_naddr;
	ext2->addr_per_blk = 1 << ext2->log_naddr;
	ext2->addr_per_blk_sq = 1 << ext2->log_naddr_sq;

	ext2->bgd_per_blk = ext2->blksz >> EXT2_BGD_LOG_SZ;
	ext2->nbgd = sblock->s_blocks_count / sblock->s_blocks_per_group;
	if(ext2->nbgd * sblock->s_blocks_per_group < sblock->s_blocks_count) {
		ext2->nbgd++;
	}

	if(sblock->s_rev_level == EXT2_GOOD_OLD_REV) {
		ext2->ino_sz = EXT2_OLD_INODE_SIZE;
	} else {
		ext2->ino_sz = sblock->s_inode_size;
	}

	ext2->ino_per_blk = ext2->blksz / ext2->ino_sz;

	/*
	 * TODO That's taken from FreeBSD, but doesn't that value depend on
	 * block size?
	 */
	if(sblock->s_rev_level == EXT2_GOOD_OLD_REV ||
		!(sblock->s_feature_ro_compat &
		EXT2_FEATURE_RO_COMPAT_LARGE_FILE))
	{
		ext2->maxlen = 0x7fffffff;
	} else if(sblock->s_feature_ro_compat &
		EXT2_FEATURE_RO_COMPAT_LARGE_FILE)
	{
		ext2->maxlen = 0x7fffffffffffffffULL;
	} else {
		ext2->maxlen = 0xffffffffffffULL;
	}

	return true;
}

static int ext2_mount(filesys_t *fs, ino_t *root) {
	ext2_fs_t *ext2;
	int err;

	ext2 = kmalloc(sizeof(*ext2), VM_WAIT);

	ext2->dev = fs->dev;
	sync_init(&ext2->alloc_lock, SYNC_MUTEX);

	err = bread(ext2->dev, EXT2_SBLOCK_OFF, sizeof(ext2_sblock_t),
		&ext2->super);
	if(err) {
		goto err_sb;
	}

	/*
	 * Validate the superblock and retrieve get some useful information.
	 */
	if(!ext2_parse_super(ext2, &ext2->super)) {
		err = -EINVAL;
		goto err_sb;
	}

	ext2->bgds = kmalloc(sizeof(ext2_bgd_t) * ext2->nbgd, VM_WAIT);
	ext2->bgd_blks = ALIGN(ext2->nbgd << EXT2_BGD_LOG_SZ, ext2->blksz) >>
		ext2->blkshift;

	/*
	 * Read the block group descriptors.
	 */
	for(size_t i = 0; i < ext2->bgd_blks; i++) {
		uint32_t num, blk = ext2->super.s_first_data_block + 1 + i;

		num = min(ext2->bgd_per_blk, ext2->nbgd -
			i * ext2->bgd_per_blk);
		err = bread(fs->dev, EXT2_BOFF(ext2, blk, 0),
			num << EXT2_BGD_LOG_SZ,
			&ext2->bgds[i * ext2->bgd_per_blk]);
		if(err) {
			goto err_bgd;
		}
	}

	/*
	 * Mark ext2 dirty TODO NOT IF READ ONLY.
	 */
	ext2->super.s_state = EXT2_ERROR_FS;
	bwrite(ext2->dev, EXT2_SBLOCK_OFF, sizeof(ext2_sblock_t),
		&ext2->super);

	filesys_set_priv(fs, ext2);
	*root = EXT2_INO_ROOT;

	return 0;

err_bgd:
	kfree(ext2->bgds);
err_sb:
	sync_destroy(&ext2->alloc_lock);
	kfree(ext2);
	return err;
}

static void ext2_unmount(filesys_t *fs) {
	ext2_fs_t *ext2 = filesys_get_priv(fs);
	sync_destroy(&ext2->alloc_lock);
	kpanic("unmount");
}

static FS_DRIVER(ext2fs) {
	.name = "ext2",
	.flags = FSDRV_SOURCE,
	.mount = ext2_mount,
	.unmount = ext2_unmount,
	.vget = ext2_vget,
	.vput = ext2_vput,
	.statfs = ext2_statfs,
};

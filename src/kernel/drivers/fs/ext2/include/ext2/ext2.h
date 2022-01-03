#ifndef EXT2_H
#define EXT2_H

#include <kern/sync.h>
#include <vfs/vnode.h>
#include <block/block.h>

struct filesys;

#define EXT2_SBLOCK_OFF	1024
#define EXT2_INO_ROOT	2

#define EXT2_OLD_INODE_SIZE 128

#define EXT2_MIN_BLKSZ 1024
#define EXT2_MIN_BLKSZ_LOG 10

/**
 * A block group descriptor has 32 bytes.
 */
#define EXT2_BGD_LOG_SZ		5
#define EXT2_ADDR_LOG_SZ	2

#define	EXT2_NDIR_BLOCKS	12
#define	EXT2_IND_BLOCK		EXT2_NDIR_BLOCKS
#define	EXT2_DIND_BLOCK		(EXT2_IND_BLOCK + 1)
#define	EXT2_TIND_BLOCK		(EXT2_DIND_BLOCK + 1)
#define	EXT2_N_BLOCKS		(EXT2_TIND_BLOCK + 1)

#define EXT2_SYMLINK_INLINE	(EXT2_N_BLOCKS * sizeof(uint32_t))

/**
 * i_blocks counts the number of 512 byte blocks and not the number of filsystem
 * blocks. One filesystem block has EXT2_IBLKS(fs) 512 byte blocks.
 */
#define EXT2_IBLKS(fs)		((fs)->blksz >> 9)

#define EXT2_BOFF(fs, blk, off) ({		\
	assert((off) < (fs)->blksz);		\
	((blk) << (fs)->blkshift) + (off);	\
})

#define EXT2_DENT_HOLE(dent) ({				 \
	(dent)->rec_len - EXT2_RECLEN((dent)->name_len); \
})

#define EXT2_SOFF(member) (EXT2_SBLOCK_OFF + offsetof(ext2_sblock_t, member))

#define ext2_ino_bgd(fs, ino) ({					\
	&(fs)->bgds[((ino) - 1) / (fs)->super.s_inodes_per_group];	\
})

typedef struct ext2_sblock {
	uint32_t s_inodes_count;
	uint32_t s_blocks_count;

	/* TODO implement. */
	uint32_t s_r_blocks_count;
	uint32_t s_free_blocks_count;
	uint32_t s_free_inodes_count;
	uint32_t s_first_data_block;
	uint32_t s_log_block_size;
	uint32_t s_log_frag_size;
	uint32_t s_blocks_per_group;
	uint32_t s_frags_per_group;
	uint32_t s_inodes_per_group;
	uint32_t s_mtime;
	uint32_t s_wtime;
	uint16_t s_mnt_count;
	uint16_t s_max_mnt_count;

#define EXT2_SUPER_MAGIC		0xEF53
	uint16_t s_magic;

#define EXT2_VALID_FS			1
#define EXT2_ERROR_FS			2
	uint16_t s_state;

#define EXT2_ERRORS_CONTINUE		1
#define EXT2_ERRORS_RO			2
#define EXT2_ERRORS_PANIC		3
	uint16_t s_errors;
	uint16_t s_minor_rev_level;
	uint32_t s_lastcheck;
	uint32_t s_checkinterval;
	uint32_t s_creator_os;

#define EXT2_GOOD_OLD_REV		0
#define EXT2_DYNAMIC_REV		1
	uint32_t s_rev_level;
	uint16_t s_def_resuid;
	uint16_t s_def_resgid;

	/* EXT2_DYNAMIC_REV specific */
	uint32_t s_first_ino;
	uint16_t s_inode_size;
	uint16_t s_block_group_nr;

#define EXT2_FEATURE_COMPAT_DIR_PREALLOC	0x0001 /* Block pre-allocation for new directories */
#define EXT2_FEATURE_COMPAT_IMAGIC_INODES	0x0002
#define EXT3_FEATURE_COMPAT_HAS_JOURNAL		0x0004 /* An Ext3 journal exists */
#define EXT2_FEATURE_COMPAT_EXT_ATTR		0x0008 /* Extended inode attributes are present */
#define EXT2_FEATURE_COMPAT_RESIZE_INO		0x0010 /* Non-standard inode size used */
#define EXT2_FEATURE_COMPAT_DIR_INDEX		0x0020 /* Directory indexing (HTree) */
	uint32_t s_feature_compat;

#define EXT2_FEATURE_INCOMPAT_COMPRESSION	0x0001
#define EXT2_FEATURE_INCOMPAT_FILETYPE		0x0002
#define EXT3_FEATURE_INCOMPAT_RECOVER		0x0004
#define EXT3_FEATURE_INCOMPAT_JOURNAL_DEV	0x0008
#define EXT2_FEATURE_INCOMPAT_META_BG		0x0010
	uint32_t s_feature_incompat;

#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER	0x0001
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE	0x0002
#define EXT2_FEATURE_RO_COMPAT_BTREE_DIR	0x0004
	uint32_t s_feature_ro_compat;
	uint8_t  s_uuid[16];
	uint8_t	 s_volume_name[16];
	uint8_t  s_last_mounted[64];
	uint32_t s_algo_bitmap;

	/*
	 * Performance hints
	 */
	uint8_t  s_prealloc_blocks;
	uint8_t  s_prealloc_dir_blocks;
	uint16_t pad0;

	/*
	 * Journaling Support
	 */
	uint8_t  s_journal_uuid[16];
	uint32_t s_journal_inum;
	uint32_t s_journal_dev;
	uint32_t s_last_orphan;

	/*
	 * Directory Indexing Support
	 */
	uint32_t s_hash_seed[4];
	uint8_t  s_def_hash_version;
	uint8_t  pad1[3];

	/*
	 * Other options
	 */
	uint32_t s_default_mount_options;
	uint32_t s_first_meta_bg;
} __packed ext2_sblock_t;

typedef struct ext2_bgd {
	uint32_t bg_block_bitmap;
	uint32_t bg_inode_bitmap;
	uint32_t bg_inode_table;
	uint16_t bg_free_blocks_count;
	uint16_t bg_free_inodes_count;
	uint16_t bg_used_dirs_count;
	uint16_t bg_pad;
	uint8_t  bg_rsvd[12];
} __packed ext2_bgd_t;

typedef struct ext2_inode {
	uint16_t i_mode;
	uint16_t i_uid;
	uint32_t i_size;
	uint32_t i_atime;
	uint32_t i_ctime;
	uint32_t i_mtime;
	uint32_t i_dtime;
	uint16_t i_gid;
	uint16_t i_nlink;
	uint32_t i_blocks; /* Number of 512 byte blocks */

#define EXT2_SECRM_FL		0x00000001
#define EXT2_UNRM_FL		0x00000002
#define EXT2_COMPR_FL		0x00000004
#define EXT2_SYNC_FL		0x00000008
#define EXT2_IMMUTABLE_FL	0x00000010
#define EXT2_APPEND_FL		0x00000020
#define EXT2_NODUMP_FL		0x00000040
#define EXT2_NOATIME_FL		0x00000080
#define EXT2_DIRTY_FL		0x00000100
#define EXT2_COMPRBLK_FL	0x00000200
#define EXT2_NOCOMPR_FL		0x00000400
#define EXT2_ECOMPR_FL		0x00000800
#define EXT2_BTREE_FL		0x00001000
#define EXT2_INDEX_FL		0x00002000
#define EXT2_IMAGIC_FL		0x00004000
#define EXT3_JOURNAL_DATA_FL	0x00008000
	uint32_t i_flags;
	uint32_t i_osd1;
	uint32_t i_block[EXT2_N_BLOCKS];
	uint32_t i_gen;
	uint32_t i_file_acl;
	uint32_t i_dir_acl;
	uint32_t i_faddr;
	uint8_t  i_osd2[12];
} __packed ext2_inode_t;

#define EXT2_DENT_SZ 		8u /* ext2_dent size without name */
#define EXT2_NAMELEN 		255u
#define EXT2_RECLEN(namelen)	ALIGN(EXT2_DENT_SZ + (namelen), 4)
#define EXT2_DENT_MAX 		EXT2_RECLEN(EXT2_NAMELEN)
#define EXT2_DOT_IDX		0
#define EXT2_DOT_DOT_IDX	EXT2_RECLEN(1) /* sizeof "." entry */
typedef struct ext2_dent {
	uint32_t inode;
	uint16_t rec_len;
	uint8_t  name_len;

#define EXT2_FT_UNKNOWN		0
#define EXT2_FT_REG_FILE	1
#define EXT2_FT_DIR		2
#define EXT2_FT_CHRDEV		3
#define EXT2_FT_BLKDEV		4
#define EXT2_FT_FIFO		5
#define EXT2_FT_SOCK		6
#define EXT2_FT_SYMLINK		7
	uint8_t  file_type;

	/*
	 * Actually this is 255 but it's convenient to be able to add the
	 * trailing '\0' directly in the buffer.
	 */
	char name[EXT2_NAMELEN + 1];
} __packed ext2_dent_t;

#define EXT2_VTOPRIV(vn)	((ext2_priv_t *)vnode_priv(vn))
#define EXT2_VTOI(vn) 		(&EXT2_VTOPRIV(vn)->inode)
#define EXT2_VTONAMEI(vn)	(&EXT2_VTOPRIV(vn)->namei)
typedef struct ext2_priv {
	ext2_inode_t inode;
	vnamei_aux_t namei;
} ext2_priv_t;

/**
 * Maybe consider storing some free blocks numbers in memory. SEARCH free
 * blocks when running idle.
 */
typedef struct ext2_fs {
	ext2_sblock_t super;
	sync_t alloc_lock;

	struct blk_provider *dev;
	size_t blksz;
	size_t blkshift;
	size_t ino_sz;

	size_t addr_per_blk;
	size_t addr_per_blk_sq;
	size_t log_naddr; /* log2(addr_per_blk) */
	size_t log_naddr_sq; /* log2(addr_per_blk * addr_per_blk) */

	size_t bgd_per_blk;
	size_t ino_per_blk;

	size_t bgd_blks; /* number of blocks containing bg descriptors */
	size_t nbgd;
	ext2_bgd_t *bgds;

	uint64_t maxlen;
} ext2_fs_t;

#if 1
#define ext2_dbg(fmt...) kprintf("[ext2] " fmt)
#else
#define ext2_dbg(fmt...)
#endif

extern vnode_ops_t ext2_vnops;

/* Temporary */
int ext2_sync(ext2_fs_t *fs);

vnode_t *ext2_valloc(struct filesys *fs);
void ext2_vfree(vnode_t *node);

int ext2_get_inode(ext2_fs_t *fs, uint32_t ino, ext2_inode_t *inode);
int ext2_set_inode(ext2_fs_t *fs, uint32_t ino, ext2_inode_t *inode);
int ext2_get_bgd(ext2_fs_t *fs, uint32_t id, ext2_bgd_t *bgd);
int ext2_set_bgd(ext2_fs_t *fs, uint32_t id, ext2_bgd_t *bgd);

int ext2_balloc(ext2_fs_t *fs, vnode_t *node, uint32_t *blkp);
void ext2_bfree(ext2_fs_t *fs, uint32_t blk);

/**
 * @brief Zero a block.
 */
int ext2_bclr(ext2_fs_t *fs, uint32_t blk);

int ext2_ialloc(ext2_fs_t *fs, uint32_t *res);
void ext2_ifree(ext2_fs_t *fs, uint32_t ino);

int ext2_inode_truncate(ext2_fs_t *fs, vnode_t *node, vnode_size_t length);
int ext2_inode_bmap(ext2_fs_t *fs, vnode_t *node, bool alloc, blkno_t lbn,
	blkno_t *pbn);

/**
 * @brief Get a directory entry.
 * @return	0 	success
 *		< 0	error
 *		1 	end of directory
 */
int ext2_getdent(ext2_fs_t *fs, vnode_t *node, vnode_size_t off, blkno_t *lbn,
	blkno_t *pbn, ext2_dent_t *dent);

int ext2_add_dent(ext2_fs_t *fs, vnode_t *dir, const char *name, size_t namelen,
	vnode_t *node);

/**
 * @brief Check if a directory is empty (i.e. only "." and ".." dents).
 * @return	< 0	error
 * 		0 	directory is not empty
 *		1 	directory is empty
 */
int ext2_dirempty(ext2_fs_t *fs, vnode_t *dir);
int ext2_rmdent(ext2_fs_t *fs, vnode_t *node);

#endif

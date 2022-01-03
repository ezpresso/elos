#ifndef SYS_STAT_H
#define SYS_STAT_H

#define __NEED_DEV_T
#define __NEED_MODE_T
#define __NEED_NLINK_T
#define __NEED_INO_T
#define __NEED_UID_T
#define __NEED_GID_T
#define __NEED_OFF_T
#define __NEED_BLKSIZE_T
#define __NEED_BLKCNT_T
#define __NEED_STRUCT_TIMESPEC
#include <sys/alltypes.h>

#define S_IFMT  0170000

#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFBLK 0060000
#define S_IFREG 0100000
#define S_IFIFO 0010000
#define S_IFLNK 0120000
#define S_IFSOCK 0140000

#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#define S_ISCHR(mode)  (((mode) & S_IFMT) == S_IFCHR)
#define S_ISBLK(mode)  (((mode) & S_IFMT) == S_IFBLK)
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#define S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFIFO)
#define S_ISLNK(mode)  (((mode) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(mode) (((mode) & S_IFMT) == S_IFSOCK)

#define UTIME_NOW  0x3fffffff
#define UTIME_OMIT 0x3ffffffe

struct stat64 {
	dev_t st_dev;
	int __st_dev_padding;
	long __st_ino_truncated;
	mode_t st_mode;
	nlink_t st_nlink;
	uid_t st_uid;
	gid_t st_gid;
	dev_t st_rdev;
	int __st_rdev_padding;
	off_t st_size;
	blksize_t st_blksize;
	blkcnt_t st_blocks;
	struct timespec st_atim;
	struct timespec st_mtim;
	struct timespec st_ctim;
	ino_t st_ino;
};

#endif

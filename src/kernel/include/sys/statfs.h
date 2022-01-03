#ifndef SYS_STATFS_H
#define SYS_STATFS_H

#define __NEED_FSBLKCNT_T
#define __NEED_FSFILCNT_T
#include <sys/alltypes.h>

typedef struct fsid {
	int val[2];
} fsid_t;

struct statfs {
	unsigned long f_type, f_bsize;
	fsblkcnt_t f_blocks, f_bfree, f_bavail;
	fsfilcnt_t f_files, f_ffree;
	fsid_t f_fsid;
	unsigned long f_namelen, f_frsize, f_flags, f_spare[4];
};

#endif

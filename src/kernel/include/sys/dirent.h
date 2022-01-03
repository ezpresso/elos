#ifndef SYS_DIRENT_H
#define SYS_DIRENT_H

#define __NEED_INO_T
#define __NEED_OFF_T
#include <sys/alltypes.h>

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14

/* linux_dirent64 */
struct dirent {
	ino_t d_ino;
	off_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[255];
};

#ifdef __KERNEL__
/**
 * Minimum size of a dirent (cannot use sizeof(struct kdirent) here)
 */
#define DIRENT_SZ 19
#define DIRENT_LEN(namelen) \
	ALIGN(DIRENT_SZ + namelen + 1, __alignof__(struct dirent *))

/* used internally by kernel (the name is not in this structure which allows
 * dirents to be on the small kernel stack)
 */
struct kdirent {
	ino_t d_ino;
	off_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[0];
};

#endif

#endif

#ifndef SYS_FCNTL_H
#define SYS_FCNTL_H

#define O_SEARCH		010000000
#define O_EXEC 			010000000
#define O_PATH			010000000

#define O_ACCMODE		(03|O_SEARCH)
#define O_RDONLY 		00
#define O_WRONLY 		01
#define O_RDWR 			02

#define O_CREAT 		0100
#define O_EXCL 			0200
#define O_NOCTTY 		0400
#define O_TRUNC 		01000
#define O_APPEND 		02000
#define O_NONBLOCK 		04000
#define O_DSYNC 		010000
#define O_SYNC 			04010000
#define O_RSYNC 		04010000
#define O_DIRECTORY		0200000
#define O_NOFOLLOW		0400000
#define O_CLOEXEC		02000000
#define O_ASYNC 		020000
#define O_DIRECT		040000
#define O_LARGEFILE 		0100000
#define O_NOATIME 		01000000
#define O_TMPFILE 		020200000
#define O_NDELAY 		O_NONBLOCK

#define F_DUPFD  0
#define F_GETFD  1
#define F_SETFD  2
#define F_GETFL  3
#define F_SETFL  4

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

#define F_DUPFD_CLOEXEC 1030

#define FD_CLOEXEC 1

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXU 0700
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXG 0070
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_IRWXO 0007

#ifdef __KERNEL__
#define CHMOD_BITS (S_ISUID | S_ISGID | S_ISVTX | S_IRUSR | S_IWUSR | S_IXUSR \
		| S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH)
#define S_ICHECK(mode) \
	(((mode) & ~(S_ISUID | S_ISGID | S_ISVTX)) <= 0777)

#endif

#define AT_FDCWD 		(-100)
#define AT_SYMLINK_NOFOLLOW	0x100
#define AT_REMOVEDIR		0x200
#define AT_SYMLINK_FOLLOW 	0x400
#define AT_EACCESS		0x200
#define AT_NO_AUTOMOUNT		0x800
#define AT_EMPTY_PATH		0x1000

#endif
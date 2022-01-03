
#include <sys/arch/types.h>

#ifndef _addr
#define _addr int
#endif

#ifndef _int64
#define _int64 long long
#endif

#if (defined(__NEED_INT_T) || defined(__NEED_OFF_T) || defined(__NEED_DEV_T) || \
	defined(__NEED_INO_T)) && !defined(__INT_T)
#define __INT_T
typedef signed char 	int8_t;
typedef short		int16_t;
typedef int		int32_t;
typedef _int64		int64_t;
typedef _int64		intmax_t;
typedef _addr		intptr_t;

typedef unsigned char	uint8_t;
typedef unsigned short	uint16_t;
typedef unsigned int 	uint32_t;
typedef unsigned _int64 uint64_t;
typedef unsigned _int64	uintmax_t;
typedef unsigned _addr	uintptr_t;
#endif

#if defined(__NEED_OFF_T) && !defined(__OFF_T)
#define __OFF_T
typedef int64_t	off_t;
#endif

#if defined(__NEED_DEV_T) && !defined(__DEV_T)
#define __DEV_T
typedef uint64_t dev_t;
#endif

#if defined(__NEED_BLKCNT_T) && !defined(__BLKCNT_T)
#define __BLKCNT_T
typedef _int64 blkcnt_t;
#endif

#if defined(__NEED_BLKSIZE_T) && !defined(__BLKSIZE_T)
#define __BLKSIZE_T
typedef long blksize_t;
#endif

#if defined(__NEED_GID_T) && !defined(__GID_T)
#define __GID_T
typedef unsigned gid_t;
#endif

#if defined(__NEED_INO_T) && !defined(__INO_T)
#define __INO_T
typedef uint64_t ino_t;
#endif

#if defined(__NEED_MODE_T) && !defined(__MODE_T)
#define __MODE_T
typedef unsigned mode_t;
#endif

#if defined(__NEED_NLINK_T) && !defined(__NLINK_T)
#define __NLINK_T
typedef unsigned int nlink_t;
#endif

#if defined(__NEED_PID_T) && !defined(__PID_T)
#define __PID_T
typedef int pid_t;
#endif

#if defined(__NEED_UID_T) && !defined(__UID_T)
#define __UID_T
typedef unsigned uid_t;
#endif

#if defined(__NEED_SIZE_T) && !defined(__SIZE_T)
#define __SIZE_T
typedef unsigned _addr size_t;
#endif

#if defined(__NEED_SSIZE_T) && !defined(__SSIZE_T)
#define __SSIZE_T
typedef _addr ssize_t;
#endif

#if (defined(__NEED_TIME_T) || defined(__NEED_STRUCT_TIMESPEC) || \
	defined(__NEED_STRUCT_TIMEVAL)) && !defined(__TIME_T)
#define __TIME_T
typedef long time_t;
#endif

#if (defined(__NEED_SUSECONDS_T) || defined(__NEED_STRUCT_TIMEVAL)) && \
	!defined(__SUSECONDS_T)
#define __SUSECONDS_T
typedef long suseconds_t;
#endif

#if defined(__NEED_CLOCKID_T) && !defined(__CLOCKID_T)
#define __CLOCKID_T
typedef int clockid_t;
#endif

#if defined(__NEED_STRUCT_TIMESPEC) && !defined(__STRUCT_TIMESPEC)
#define __STRUCT_TIMESPEC
struct timespec {
	time_t	tv_sec;
	long	tv_nsec;
};
#endif

#if defined(__NEED_STRUCT_TIMEVAL) && !defined(___STRUCT_TIMEVAL)
#define ___STRUCT_TIMEVAL
struct timeval {
	time_t		tv_sec;
	suseconds_t	tv_usec;
};
#endif

#if defined(__NEED_FSBLKCNT_T) && !defined(__FSBLKCNT_T)
#define __FSBLKCNT_T
typedef unsigned _int64 fsblkcnt_t;
#endif

#if defined(__NEED_FSFILCNT_T) && !defined(__FSFILCNT_T)
#define __FSFILCNT_T
typedef unsigned _int64 fsfilcnt_t;
#endif

#undef _addr
#undef _int64

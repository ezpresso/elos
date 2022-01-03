#ifndef SYS_LIMITS_H
#define SYS_LIMITS_H

#include <sys/arch/limits.h>

#define NAME_MAX	255
#define PATH_MAX	4096
#define IOV_MAX		1024
#define ARG_MAX 	131072

#define CHAR_BIT 8
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255
#define SHRT_MIN  (-1-0x7fff)
#define SHRT_MAX  0x7fff
#define USHRT_MAX 0xffff
#define INT_MIN  (-1-0x7fffffff)
#define INT_MAX  0x7fffffff
#define UINT_MAX 0xffffffffU
#define LONG_MIN (-LONG_MAX-1)
#define ULONG_MAX (2UL*LONG_MAX+1)
#define LLONG_MIN (-LLONG_MAX-1)
#define ULLONG_MAX (2ULL*LLONG_MAX+1)

#define SSIZE_MAX LONG_MAX
#define SIZE_MAX  ((size_t)-1) /* TODO */

#endif

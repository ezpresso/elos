#ifndef SYS_STDINT_H
#define SYS_STDINT_H

#define __NEED_INT_T
#include <sys/alltypes.h>

#define INT8_MIN	(-1-0x7f)
#define INT16_MIN	(-1-0x7fff)
#define INT32_MIN	(-1-0x7fffffff)
#define INT64_MIN	(-1-0x7fffffffffffffff)

#define INT8_MAX	(0x7f)
#define INT16_MAX	(0x7fff)
#define INT32_MAX	(0x7fffffff)
#define INT64_MAX	(0x7fffffffffffffff)

#define UINT8_MAX 	(0xff)
#define UINT16_MAX	(0xffff)
#define UINT32_MAX	(0xffffffff)
#define UINT64_MAX	(0xffffffffffffffff)

#endif

#ifndef SYS_POW2_H
#define SYS_POW2_H

#include <sys/limits.h>

/* TODO move to somewhere else */
#define UINTMAX(x) _Generic((x), \
	unsigned int: UINT_MAX, \
	unsigned long: ULONG_MAX, \
	unsigned long long: ULLONG_MAX)

#define pow2_p(x) ({ \
	typeof(x) _x = (x); \
	((_x != 0) && ((_x & (~_x + 1)) == _x)); \
})

#define next_pow2(x) ({ \
	typeof(x) _x = (x); \
	assert((_x) > 0); \
	assert((_x) <= (UINTMAX(x) >> 1) + 1); \
	((typeof(_x))1 << ((sizeof(_x) * 8) - _Generic((_x), \
		unsigned int: __builtin_clz, \
		unsigned long: __builtin_clzl, \
		unsigned long long: __builtin_clzll)((_x) - 1))); \
})

#endif

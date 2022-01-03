#ifndef __ELOS_ELOS_H__
#define __ELOS_ELOS_H__

#define BUILD_DATE 		__DATE__ " " __TIME__

#define KB_SHIFT		10
#define MB_SHIFT		20
#define GB_SHIFT		30
#define KB			(1 << KB_SHIFT)
#define MB			(1 << MB_SHIFT)
#define GB			(1 << GB_SHIFT)

#define CONCAT_I(a, b) 		a ## b
#define CONCAT(a, b) 		CONCAT_I(a, b)
#define UNIQUE_NAME(base) 	CONCAT(base, __COUNTER__)
#define STR_I(s) 		# s
#define STR(s) 			STR_I(s)
#define DECONST(type, var)	((type)(uintptr_t)(const void *)(var))

#define ALIGN_MASK(x, mask)	(((x) + (mask)) & ~(mask))
#define ALIGN(x, a)		ALIGN_MASK(x, (typeof(x))(a) - 1)
#define ALIGN_PTR(ptr, a)	((void*)ALIGN((uintptr_t)(ptr), a))
#define ALIGN_DOWN(addr, size)  ((addr) & (~((size) - 1)))
#define ALIGN_PTR_DOWN(ptr, a)	(void*)ALIGN_DOWN((uintptr_t)(ptr), a)
#define ALIGNED(x, a)		(((x) & ((typeof((x)))(a) - 1)) == 0)
#define PTR_ALIGNED(ptr, a)	ALIGNED((uintptr_t)(ptr), a)

#define auto_t 			__auto_type

#define attribute(x) 		__attribute__((x))
#define asmlinkage		__attribute__((regparm(0)))
#define __unused 		__attribute__((unused))
#define __const			__attribute__((const))
#define __used 			__attribute__((used))
#define __section(x) 		__attribute__((__section__(x)))
#define __packed 		__attribute__((packed))
#define __noreturn 		__attribute__((noreturn))
#define __align(x) 		__attribute__((aligned(x)))
#define __must_check 		__attribute__((warn_unused_result))
#define __always_inline 	__attribute__((always_inline))
#define __cleanup(func) 	__attribute__((cleanup(func)))
#define __cleanup_var(fn, type)	__unused type __cleanup(fn) UNIQUE_NAME(cleanup)
#define __deprecated		__attribute__((deprecated))
#define __rettwice		__attribute__((returns_twice))
#define __pure			__attribute__((pure))
#define __const			__attribute__((const))
#define __printf_format(s, f)	__attribute__((format (printf, s, f)));

#define NELEM(x)		(sizeof(x)/sizeof((x)[0]))
#define unlikely(x)		__builtin_expect((x), 0)
#define likely(x)		__builtin_expect((x), 1)
#define notreached()		__builtin_unreachable()
#define offsetof(st, m)		__builtin_offsetof(st, m)
#define container_of(ptr, type, member) ({			\
	((type *)((void *)(ptr) - offsetof(type, member)));	\
})

/**
 * #if notyet
 *	... some thoughts about new code ...
 * #endif
 */
#define notyet 0

/**
 * @brief Declare a static assertion.
 */
#define ASSERT(cond, msg) _Static_assert(cond, msg)

/**
 * Usage:
 *	for(int i = 0, __for_loop_initcode(
 *			kprintf("Hello World");
 *			....
 * 		));
 *		i < 5;
 *		i++) { .... }
 * Without i:
 * for(int __for_loop_initcode(
 *			kprintf("Hello World");
 * 		));
 *		....) { ... }
 */
#define __for_loop_initcode(code) \
	*__unused UNIQUE_NAME(__make_gcc_happy) = ({ code NULL; })

#define SWAP(a, b) ({ \
	typeof(a) tmp = (a); \
	(a) = (b); \
	(b) = tmp; \
})

#define ABS(a) ((a) > 0 ? (a) : (-a))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(d, min, max) MAX(MIN(d, max), min)

#define abs(a) ({ \
	typeof(a) _a = (a); \
	((_a) > 0 ? (_a) : (-_a)); \
})

#define min(a, b) ({ \
	typeof(a) _a = (a); \
	typeof(b) _b = (b); \
	(((_a) < (_b)) ? (_a) : (_b)); \
})

#define max(a, b)({ \
	typeof(a) _a = (a); \
	typeof(b) _b = (b); \
	(((_a) > (_b)) ? (_a) : (_b)); \
})

#define clamp(d, _min, _max) max(min(d, _max), _min)

#endif

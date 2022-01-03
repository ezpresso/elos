#ifndef __ELOS_BITOPS_H__
#define __ELOS_BITOPS_H__

#define F_ISSET(x,f)	((x) & (f))
#define F_ISALLSET(x,f) (((x) & (f)) == (f))
#define F_CLR(x,f)	((x) &= ~(f))
#define F_SET(x,f)	((x) |= (f))

#define bclr(x, b)	({ *(x) &= ~(((typeof(*x)) 1) << (b)); })
#define bset(x, b)	({ *(x) |=  (((typeof(*x)) 1) << (b)); })
#define ispow2(x) 	(((x) != 0) && (((x) & (~(x) + 1)) == (x)))

#define MASK(intlen, endbit, startbit) 					\
	((uint ## intlen ## _t)(((1ULL << ((endbit) + 1)) - 1) &	\
	~((1ULL << startbit) - 1)))

#define GETBITS(i, off, len) \
	(((i) >> (off)) & ((1ULL << (len)) - 1))

#define SETBITS(res, val, off, len)			\
	(((res) & ~(((1ULL << len) - 1) << (off))) |	\
	(((val) & ((1ULL << len) - 1)) << (off)))

#define ffs(x) _Generic((x), \
	long: __builtin_ffsl, \
	unsigned long: __builtin_ffsl, \
	long long: __builtin_ffsll, \
	unsigned long long: __builtin_ffsll, \
	default: __builtin_ffs)(x)

#endif

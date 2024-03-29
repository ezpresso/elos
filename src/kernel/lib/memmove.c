#include <kern/system.h>
#include <kern/symbol.h>
#include <lib/string.h>

/*
 * copied from musl-libc
 */
#define WT size_t
#define WS (sizeof(WT))
#undef memmove

void *memmove(void *dest, const void *src, size_t n) {
	char *d = dest;
	const char *s = src;

	if(d == s) {
		return d;
	}

	if(s + n <= d || d + n <= s) {
		return memcpy(d, s, n);
	}

	if(d < s) {
		if((uintptr_t)s % WS == (uintptr_t)d % WS) {
			while((uintptr_t)d % WS) {
				if(!n--) {
					return dest;
				}

				*d++ = *s++;
			}

			for(; n >= WS; n -= WS, d += WS, s += WS) {
				*(WT *)d = *(WT *)s;
			}
		}

		for(; n; n--) *d++ = *s++;
	} else {
		if((uintptr_t)s % WS == (uintptr_t)d % WS) {
			while((uintptr_t)(d+n) % WS) {
				if(!n--) {
					return dest;
				}

				d[n] = s[n];
			}

			while(n >= WS) n -= WS, *(WT *)(d+n) = *(WT *)(s+n);
		}

		while(n) n--, d[n] = s[n];
	}

	return dest;
}

export(memmove);

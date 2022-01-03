#ifndef KERN_SETJMP_H
#define KERN_SETJMP_H

#include <arch/setjmp.h>

static inline __rettwice int setjmp(jmp_buf_t *buf) {
	return arch_setjmp(buf);
}

static inline void __noreturn longjmp(jmp_buf_t *buf, int val) {
	kassert(val != 0, "[setjmp] longjmp: invalid jump return value: %d",
		val);
	arch_longjmp(buf, val);
}

#endif
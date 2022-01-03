#ifndef ARCH_STACKTRACE_H
#define ARCH_STACKTRACE_H

#include <vm/layout.h>

static inline void *stacktrace_start(void) {
	void *ptr;

	asm volatile ("movl %%ebp, %0" : "=rm" (ptr));
	if(!VM_IS_KERN(ptr)) {
		return NULL;
	} else {
		return ptr;
	}
}

static inline bool stacktrace_next(void **ptr, __unused uintptr_t *ip) {
	struct stackframe {
		struct stackframe *next;
		uintptr_t addr;
	} *frame = *ptr;

	if(frame == NULL || !VM_IS_KERN(frame)) {
		return false;
	} else {
		*ptr = frame->next;
		*ip = frame->addr;
		return true;
	}
}

#endif
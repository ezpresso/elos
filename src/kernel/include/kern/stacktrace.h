#ifndef KERN_STACKTRACE_H
#define KERN_STACKTRACE_H

/**
 * The architecture defines:
 * - void *stacktrace_start();
 * - bool stacktrace_next(void **ptr, uintptr_t *ip);
 */
#include <arch/stacktrace.h>

#define STACKTRACE_STOP ((uintptr_t)-1) /* TODO UINTPTR_MAX */

#define stacktrace_foreach(ip) 				\
	void *__stptr = stacktrace_start();		\
	while(stacktrace_next(&__stptr, ip))

/**
 * @brief An entry in a stacktrace buffer.
 */
typedef struct stacktrace {
	uintptr_t ip; /* Instruction pointer */
} stacktrace_t;

void stacktrace_save(stacktrace_t *trace, size_t nelem);

#endif
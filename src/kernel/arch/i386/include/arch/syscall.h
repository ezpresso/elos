#ifndef ARCH_SYSCALL_H
#define ARCH_SYSCALL_H

#define SYSARG_LL(name) long name ## _lo, long name ## _hi

/*
 * The first cast has to be uint32_t. The lower long may be negative (even i
 * the long long is not). If the first number is e.g. -2 (hex: 0xFFFFFFFE), a
 * cast to an uint32 results in 0xFFFFFFFE, whereas a cast to uint64_t would
 * result in 0xFFFFFFFFFFFFFFFE , which is obviously erroneous.
 */
#define SYSCALL_LL(name) \
	((uint32_t)(name ## _lo) | ((uint64_t)(name ## _hi) << 32))

#define SYSCALL_LLARG(name) name ## _lo, name ## _hi

struct trapframe;

void arch_syscall(struct trapframe *frame);

#endif
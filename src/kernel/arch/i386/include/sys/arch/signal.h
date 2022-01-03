#ifndef SYS_ARCH_UCONTEXT_H
#define SYS_ARCH_UCONTEXT_H

#define _NSIG 64

typedef struct sigcontext {
	/* register order is the same as in trapframe */
	uint16_t gs, __gsh;
	uint16_t fs, __fsh;
	uint16_t es, __esh;
	uint16_t ds, __dsh;
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t esp;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
	uint32_t trapno;
	uint32_t err;
	uint32_t eip;
	uint16_t cs, __csh;
	uint32_t eflags;
	uint32_t esp_at_signal;
	uint16_t ss, __ssh;

	struct fpstate *fpstate; /* defined in arch/fpu.h */
	uint32_t oldmask;
	uint32_t cr2;
} sigcontext_t;

#define UC_FP_XSTATE 0x1

#endif

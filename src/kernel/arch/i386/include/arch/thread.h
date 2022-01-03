#ifndef ARCH_THREAD_H
#define ARCH_THREAD_H

#include <arch/fpu.h>

#define THREAD_KSTACK	0x2000 /* 8 kb */

struct thread;

typedef struct context {
	uint32_t edi;
	uint32_t esi;
	uint32_t ebx;
	uint32_t ebp;
	uint32_t eflags;
	uint32_t eip;
} context_t;

typedef struct arch_thread {
	fpubuf_t fpubuf;
	fpstate_t *fpu;

	uintptr_t kern_esp; /* kernel stack top */
	context_t *context;
	uint32_t gs_base;
	uint32_t cr2;
} arch_thread_t;

struct user_desc {
	unsigned int entry_number;
	unsigned long base_addr;
	unsigned int limit;
	unsigned int seg_32bit:1;
	unsigned int contents:2;
	unsigned int read_exec_only:1;
	unsigned int limit_in_pages:1;
	unsigned int seg_not_present:1;
	unsigned int useable:1;
};

void asmlinkage context_switch(struct context **from, struct context *to);
void arch_thread_init(struct thread *thr);
void arch_uthread_setup(struct thread *thr, uintptr_t ip, uintptr_t ustack);
void arch_kthread_setup(struct thread *thr, uintptr_t ip);
void arch_thread_exit(struct thread *thr);
void arch_thread_fork(struct thread *dst, struct thread *src, uintptr_t stack,
	uintptr_t tls);
void arch_thread_switch(struct thread *to, struct thread *from);
void arch_thread_forkret(struct thread *thr);
int sys_set_thread_area(struct user_desc *desc);

#endif
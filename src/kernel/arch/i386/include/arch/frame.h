#ifndef ARCH_FRAME_H
#define ARCH_FRAME_H

#include <arch/gdt.h>

struct jmp_buf;

typedef struct trapframe {
	uint32_t gs;
	uint32_t fs;
	uint32_t es;
	uint32_t ds;

	/* registers pushed by pusha */
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t esp;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;

	uint32_t int_no;
	
	/* Pushed by HW (err_code sometimes by sw) */
	uint32_t err_code;
	uint32_t eip;
	uint32_t cs;
	uint32_t eflags;

	/* Only push'd if crossing rings */
	uint32_t useresp;
	uint32_t ss;
} __packed trapframe_t;

#define TF_USER(tf) ((tf)->cs == UCODE_SEL)

static inline uint32_t tf_syscall_num(trapframe_t *tf) {
	return tf->eax;
}

static inline void tf_set_syscall_num(trapframe_t *tf, int sysc) {
	tf->eax = sysc;
}

static inline int tf_do_syscall(trapframe_t *tf, void *addr) {
	int (*func) (unsigned int, ...) = addr;
	return func(tf->ebx, tf->ecx, tf->edx, tf->esi, tf->edi, tf->ebp);
}

static inline void tf_set_retval(trapframe_t *tf, int val) {
	tf->eax = val;
}

static inline int tf_get_retval(trapframe_t *tf) {
	return tf->eax;
}

/**
 * @brief Initialize a trapframe for a new thread.
 */
void tf_fake(trapframe_t *tf, uintptr_t ip, uintptr_t sp, bool usr);

/**
 * @brief Adjust a trapframe to the state saved inside a jmp_buf.
 */
void tf_set_jmp_buf(trapframe_t *tf, struct jmp_buf *buf);

#endif
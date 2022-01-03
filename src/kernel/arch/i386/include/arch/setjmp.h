#ifndef ARCH_SETJMP_H
#define ARCH_SETJMP_H

typedef struct jmp_buf {
	uint32_t ebx;
	uint32_t esi;
	uint32_t edi;
	uint32_t ebp;
	uint32_t esp;
	uint32_t eip;
} jmp_buf_t;

int asmlinkage arch_setjmp(jmp_buf_t *buf);
void __noreturn asmlinkage arch_longjmp(jmp_buf_t *buf, int val);

#endif
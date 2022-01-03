#ifndef FPU_H
#define FPU_H

#define FPU_REGS_SZ 	512
#define FPU_ALIGN		16

struct arch_thread;

typedef struct fpubuf {
	uint8_t regs[FPU_REGS_SZ + FPU_ALIGN];
} __packed fpubuf_t;

typedef uint8_t fpreg_t[10];

/**
 * 16-byte x87 FPU or MMX technology registers (ST0/MM0 - ST7/MM7)
 */
typedef uint32_t fpxreg_t[4];

/**
 * 16-byte XMM-register (XMM0 - XMM7 or XMM15)
 */
typedef uint32_t xmmreg_t[4];

/**
 * @brief The state returned by fsave (legacy)
 */
typedef struct fpstate_32 {
	uint32_t fcw;
	uint32_t fsw;
	uint32_t ftw;
	uint32_t ipoff;
	uint32_t cs;
	uint32_t dataoff;
	uint32_t ds;
	fpreg_t _st[8];
	uint32_t status;
} fpstate_32_t;

/**
 * @brief The fpstate returned by fxsave
 */
typedef struct fpstate {
	uint16_t fcw;
	uint16_t fsw;
	uint8_t ftw;
	uint8_t rsvd0;
	uint16_t fop;

	uint64_t rip;
	uint64_t rdp;
	uint32_t mxcsr;
	uint32_t mxcsr_mask;

	/*
	 * 8 FPU/MMX registers, 16 bytes each
	 */
	fpxreg_t st_space[8];

	/*
	 * 16 xmm regs only for 64bit mode (else 8 regs)
	 */
	xmmreg_t xmm_space[16];
	uint32_t rsvd1[24];
} fpstate_t;

void fpu_cpu_init(void);
void fpu_init(struct arch_thread *thread);
void fpu_clone(fpstate_t *dst, fpstate_t *src);
void fpu_save(fpstate_t *fpu);
void fpu_restore(fpstate_t *fpu);

int copyout_fpu(fpstate_t *fp);
int copyin_fpu(fpstate_t *fp);

#endif
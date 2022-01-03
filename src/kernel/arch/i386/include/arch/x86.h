#ifndef ARCH_X86_H
#define ARCH_X86_H

/*
 * Features returned by cpuid 1
 */
#define FEAT_FPU	(1U << 0)
#define FEAT_PSE	(1U << 3)
#define FEAT_PAE	(1U << 6)
#define FEAT_APIC	(1U << 9)
#define FEAT_MTRR	(1U << 12)
#define FEAT_PAT	(1U << 16)
#define FEAT_X2APIC	(1U << 21)

/*
 * Technically the kernel could live without APIC, however
 * ...
 */
#define CPU_FEAT 	(FEAT_FPU | FEAT_PSE | FEAT_APIC)

/*
 * EFLAGS register bits
 */
#define EFL_CF		(1U << 0) /* carry */
#define EFL_PF		(1U << 2) /* parity flag */
#define EFL_AF		(1U << 4) /* adjust flag */
#define EFL_ZF		(1U << 6) /* zero flag */
#define EFL_SF		(1U << 7) /* sign flag */
#define EFL_TF 		(1U << 8) /* trap flag (single step) */
#define EFL_IF 		(1U << 9) /* interrupt flag */
#define EFL_DF		(1U << 10) /* direction flag */
#define EFL_OF		(1U << 11) /* overflow flag */
#define EFL_IOPL(x)	((x) << 12)
#define EFL_NT		(1U << 14) /* nested task flag */
#define EFL_RF		(1U << 16) /* resume flag */
#define EFL_VM		(1U << 17) /* virtual 8086 mode flag */
#define EFL_AC		(1U << 18) /* alignment check */
#define EFL_VIF		(1U << 19) /* virtual interrupt flag */
#define EFL_VIP		(1U << 20) /* Virtual interrupt pending */
#define EFL_ID		(1U << 21) /* able to use CPUID instruction  */

#define	CR0_PE		(1U << 0) /* Enable protected mode */
#define CR0_MP		(1U << 1)
#define CR0_EM		(1U << 2)
#define CR0_TS		(1U << 3)
#define CR0_ET 		(1U << 4)
#define CR0_NE		(1U << 5)
#define	CR0_WP		(1U << 16)
#define CR0_AM		(1U << 18)
#define	CR0_NW		(1U << 29) /* No WT */
#define	CR0_CD		(1U << 30) /* Disable caching */
#define	CR0_PG		(1U << 31) /* Enable paging */

#define CR4_VME 	(1U << 0)
#define CR4_PVI		(1U << 1)
#define CR4_TSD		(1U << 2)
#define CR4_DE		(1U << 3)
#define CR4_PSE		(1U << 4)
#define CR4_PAE		(1U << 5)
#define CR4_MCE		(1U << 6)
#define CR4_PGE		(1U << 7)
#define CR4_PCE		(1U << 8)
#define CR4_OSFXSR	(1U << 9)
#define CR4_OSXMMEXCPT	(1U << 10)
#define CR4_VMXE	(1U << 13)
#define CR4_SMXE	(1U << 14)
#define CR4_FSGSBASE	(1U << 16)
#define CR4_PCIDE	(1U << 17)
#define CR4_OSXSAVE	(1U << 18)
#define CR4_SMEP	(1U << 20)
#define CR4_SMAP	(1U << 21)
#define CR4_PKE		(1U << 22)

/*
 * Page Fault Error code bits pushed onto the stack by the processor.
 */
#define PFE_P		(1U << 0) /* present */
#define PFE_W		(1U << 1) /* write */
#define PFE_U		(1U << 2) /* user access */
#define PFE_RSVD	(1U << 3) /* reserved bits were set */
#define PFE_I		(1U << 4) /* instruction fetch */
#define PFE_PK		(1U << 5) /* some protection key stuff */
#define PFE_SGX		(1U << 15)

#ifndef __ASSEMBLER__

typedef struct idt_gate {
	uint16_t base_low;
	uint16_t cs;
	uint8_t zero;
	uint8_t type:4;
	uint8_t s:1;
	uint8_t dpl:2;
	uint8_t p:1;
	uint16_t base_high;
} __packed idt_gate_t;

static inline int cpuid(uint32_t op, uint32_t *eax, uint32_t *ebx,
	uint32_t *ecx, uint32_t *edx)
{
	uint32_t _eax, _ebx, _ecx, _edx;
	asm("cpuid\n\t"
		: "=a" (_eax), "=b" (_ebx), "=c" (_ecx), "=d" (_edx)
		: "0" (op));
	if(eax) {
		*eax = _eax;
	}
	if(ebx) {
		*ebx = _ebx;
	}
	if(ecx) {
		*ecx = _ecx;
	}
	if(edx) {
		*edx = _edx;
	}

	return 1;
}

static inline __always_inline void cli(void) {
	asm volatile("cli");
}

static inline __always_inline void sti(void) {
	asm volatile("sti");
}

static inline __always_inline void hlt(void) {
	asm volatile("hlt");
}

static inline __always_inline uint32_t eflags_get(void) {
	uint32_t eflags;
	asm volatile ("pushfl; popl %0":"=r" (eflags));
	return eflags;
}

static inline __always_inline uint32_t cr0_get(void) {
	uint32_t cr0;
	asm volatile("mov %%cr0, %0" : "=r" (cr0));
	return cr0;
}

static inline __always_inline void cr0_set(uint32_t value) {
	asm volatile("mov %0, %%cr0" : : "r" (value));
}

/* page fault address */
static inline __always_inline uint32_t cr2_get(void) {
	uint32_t cr2;
	asm volatile("mov %%cr2, %0" : "=r" (cr2));
	return cr2;
}

static inline __always_inline uint32_t cr4_get(void) {
	uint32_t cr4;
	asm volatile("mov %%cr4, %0" : "=r" (cr4));
	return cr4;
}

static inline __always_inline void cr4_set(uint32_t value) {
	asm volatile("mov %0, %%cr4" : : "r" (value));
}

static inline __always_inline void wbinvd(void) {
	asm volatile("wbinvd");
}

static inline __always_inline void lidt(uintptr_t base, uintptr_t limit) {
	volatile struct {
		uint16_t limit;
		uint32_t base;
	} __packed idt_ptr = {
		.limit = limit,
		.base = base,
	};
	asm volatile ("lidt (%0)" :: "r" (&idt_ptr));
}

static inline __always_inline void ltr(uint16_t seg) {
	asm volatile ("ltr %0"::"r" (seg));
}

static inline __always_inline void outb(uint16_t port, uint8_t val) {
	asm volatile ("outb %1, %0"::"dN" (port), "a"(val));
}

static inline __always_inline uint8_t inb(uint16_t port) {
	uint8_t ret;
	asm volatile ("inb %1, %0":"=a" (ret):"dN"(port));
	return ret;
}

static inline __always_inline void outw(uint16_t port, uint16_t val) {
	asm volatile ("outw %1, %0"::"dN" (port), "a"(val));
}

static inline __always_inline uint16_t inw(uint16_t port) {
	uint16_t ret;
	asm volatile ("inw %1, %0":"=a" (ret):"dN"(port));
	return ret;
}

static inline __always_inline void outl(uint16_t port, uint32_t val) {
	asm volatile ("outl %%eax, %%dx"::"dN" (port), "a"(val));
}

static inline __always_inline uint32_t inl(uint16_t port) {
	uint32_t ret;
	asm volatile ("inl %%dx, %%eax":"=a" (ret):"dN"(port));
	return ret;
}

static inline __always_inline void outsw(uint16_t port, uint8_t * data,
	size_t size)
{
	asm volatile ("rep outsw":"+S" (data), "+c"(size):"d"(port));
}

static inline __always_inline void insw(uint16_t port, uint8_t * data,
	size_t size)
{
	asm volatile ("rep insw":"+D" (data), "+c"(size):"d"(port):"memory");
}

static inline __always_inline void fldcw(uint16_t cw) {
	asm volatile ("fldcw %0"::"m" (cw));
}

static inline __always_inline void fninit(void) {
	asm volatile ("fninit");
}

static inline __always_inline void fxsave(void *fpu) {
	asm volatile ("fxsave (%0)" :: "r" (fpu));
}

static inline __always_inline void fxrstor(void *fpu) {
	asm volatile ("fxrstor (%0)" :: "r" (fpu));
}

static inline __always_inline void invlpg(uintptr_t addr) {
	asm volatile ("movl %0, %%eax\n"
		"invlpg (%%eax)\n" :: "r" (addr) : "%eax");
}

static inline __always_inline void invltlb(void) {
	asm volatile ("movl %%cr3, %%eax\n"
		"movl %%eax, %%cr3\n" ::: "%eax");
}

static inline __always_inline void cr3_set(uint32_t value) {
	asm volatile ("mov %0, %%cr3" :: "r" (value));
}

static inline __always_inline uint64_t rdtsc(void) {
	uint64_t tsc;
	asm volatile("rdtsc" : "=A" (tsc));
	return (tsc);
}

static inline __always_inline uint64_t rdtscp(void) {
	uint64_t tsc;
	asm volatile("rdtscp" : "=A" (tsc) : : "ecx");
	return (tsc);
}

/**
 * @brief Delay for approximately 1 us.
 */
static inline void x86_io_delay(void) {
	const uint16_t DELAY_PORT = 0x80;
	asm volatile ("outb %%al, %0" :: "dN" (DELAY_PORT));
}

static inline void x86_io_udelay(int loops) {
	while(loops--) {
		x86_io_delay();
	}
}
static inline void x86_io_mdelay(int loops) {
	x86_io_udelay(loops * 1000);
}

#endif /* ifndef __ASSEMBLER __*/
#endif

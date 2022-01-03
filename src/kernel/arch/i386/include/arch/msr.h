#ifndef ARCH_MSR_H
#define ARCH_MSR_H

#define MSR_IA32_APIC_BASE			0x1b
#define 	MSR_IA32_APIC_BASE_BSP			(1U << 8)
#define 	MSR_IA32_APIC_BASE_EXTENDED		(1U << 10)
#define 	MSR_IA32_APIC_BASE_ENABLE		(1U << 11)
#define 	MSR_IA32_APIC_BASE_BASE_MASK		(0xfffffU << 12)
#define MSR_IA32_MTRR_CAP			0xfe
#define MSR_IA32_MTRR_DEF_TYPE			0x2ff
#define MSR_IA32_MTRR_PHYSBASE(n)		(0x200 + 2*(n))
#define MSR_IA32_MTRR_PHYSMASK(n)		(0x200 + 2*(n) + 1)
#define MSR_IA32_MTRR_FIX64K_00000		0x250
#define MSR_IA32_MTRR_FIX16K_80000		0x258
#define MSR_IA32_MTRR_FIX16K_A0000		0x259
#define MSR_IA32_MTRR_FIX4K_C0000		0x268
#define MSR_IA32_MTRR_FIX4K_C8000		0x269
#define MSR_IA32_MTRR_FIX4K_D0000		0x26a
#define MSR_IA32_MTRR_FIX4K_D8000		0x26b
#define MSR_IA32_MTRR_FIX4K_E0000		0x26c
#define MSR_IA32_MTRR_FIX4K_E8000		0x26d
#define MSR_IA32_MTRR_FIX4K_F0000		0x26e
#define MSR_IA32_MTRR_FIX4K_F8000		0x26f
#define MSR_IA32_BNDCFGS 			0xd90

/* TODO move to x86.h */
static inline uint64_t rdmsr64(uint32_t msr) {
	uint32_t lo = 0, hi = 0;
	asm volatile("rdmsr" : "=a" (lo), "=d" (hi) : "c" (msr));
	return (((uint64_t)hi) << 32) | ((uint64_t)lo);
}

static inline void wrmsr64(uint32_t msr, uint64_t val) {
	uint32_t lo = (val & 0xFFFFFFFF);
	uint32_t hi = ((val >> 32) & 0xFFFFFFFF);	
	asm volatile("wrmsr" : : "c" (msr), "a" (lo), "d" (hi));
}

static inline uint32_t rdmsr32(uint32_t msr) {
	uint32_t lo;
	asm volatile("rdmsr" : "=a" (lo) : "c" (msr) : "edx");
	return lo;
}

#endif

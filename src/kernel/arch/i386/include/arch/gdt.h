#ifndef ARCH_GDT_H
#define ARCH_GDT_H

#define SEG_NULL	0
#define SEG_KCODE	1
#define SEG_KDATA	2
#define SEG_UCODE	3
#define SEG_UDATA	4
#define SEG_TSS		5
#define SEG_GS		6
#define SEG_FS		7
#define SEG_CANARY	8
#define SEG_CODE16	9
#define SEG_DATA16	10
#define NGDT		11

#define DPL_KERN	0
#define DPL_USER  	3
#define SEG_SEL(s, dpl) (((s) << 3) | (dpl))

#define KCODE_SEL	SEG_SEL(SEG_KCODE, DPL_KERN)
#define KDATA_SEL	SEG_SEL(SEG_KDATA, DPL_KERN)
#define UCODE_SEL	SEG_SEL(SEG_UCODE, DPL_USER)
#define UDATA_SEL	SEG_SEL(SEG_UDATA, DPL_USER)
#define GS_SEL		SEG_SEL(SEG_GS, DPL_USER)
#define FS_SEL		SEG_SEL(SEG_FS, DPL_KERN)
#define CANARY_SEL	SEG_SEL(SEG_CANARY, DPL_KERN)

#ifdef __ASSEMBLER__

#define SEG_X 0x8
#define SEG_W 0x2
#define SEG_R 0x2

#define SEG_NULLASM		\
        .word 0, 0;		\
        .byte 0, 0, 0, 0

#define SEG_ASM(type, base, lim) \
	.word (((lim) >> 12) & 0xffff), ((base) & 0xffff); \
	.byte (((base) >> 16) & 0xff), (0x90 | (type)), \
           (0xC0 | (((lim) >> 28) & 0xf)), (((base) >> 24) & 0xff)

#else

typedef struct gdt_entry {
	unsigned limit_lo	:16;
	unsigned base_lo	:24;
	unsigned type		:5;
	unsigned dpl		:2;
	unsigned present	:1;
	unsigned limit_hi	:4;
	unsigned unused		:2;
	unsigned def32		:1;
	unsigned gran		:1;
	unsigned base_hi	:8;
} __packed gdt_entry_t;

typedef struct {
	uint16_t limit;
	uintptr_t base;
} __packed gdt_pointer_t;

typedef struct tss_entry {
	uint32_t prev_tss;
	uint32_t esp0;
	uint32_t ss0;
	uint32_t esp1;
	uint32_t ss1;
	uint32_t esp2;
	uint32_t ss2;
	uint32_t cr3;
	uint32_t eip;
	uint32_t eflags;
	uint32_t eax;
	uint32_t ecx;
	uint32_t edx;
	uint32_t ebx;
	uint32_t esp;
	uint32_t ebp;
	uint32_t esi;
	uint32_t edi;
	uint32_t es;
	uint32_t cs;
	uint32_t ss;
	uint32_t ds;
	uint32_t fs;
	uint32_t gs;
	uint32_t ldt;
	uint16_t trap;
	uint16_t iomap_base;
} __packed tss_entry_t;

void asmlinkage gdt_flush(uintptr_t);

static inline __always_inline void segment(gdt_entry_t *entry, uintptr_t base,
	size_t limit, int type, int dpl, int def32, int gran)
{
	entry->limit_lo = limit;
	entry->base_lo = base;
	entry->type = type;
	entry->dpl = dpl;
	entry->present = 1;
	entry->limit_hi = limit >> 16;
	entry->unused = 0;
	entry->def32 = def32;
	entry->gran = gran;
	entry->base_hi = base >> 24;
}

#endif /* __ASSEMBLER__ */
#endif /* ARCH_GDT_H */
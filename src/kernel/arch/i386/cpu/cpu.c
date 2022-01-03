/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 *
 * Copyright (c) 2017, Elias Zell
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <kern/system.h>
#include <kern/cpu.h>
#include <kern/proc.h>
#include <kern/init.h>
#include <kern/syscall.h>
#include <kern/sched.h>
#include <kern/mp.h>
#include <lib/string.h>
#include <arch/fpu.h>
#include <arch/kwp.h>
#include <arch/frame.h>
#include <arch/interrupt.h>

extern void *int_vectors[];
static cpu_intr_hand_t cpu_nmi_handler;
static idt_gate_t idt_gates[INT_NUM];
static cpu_intr_t cpu_intrs[INT_NUM] = {
	[INT_SYSCALL] = { .hand = (cpu_intr_hand_t *)syscall, .arg = NULL },
	[INT_NMI] = { .hand = cpu_nmi_handler, .arg = NULL },
};

cpu_vendor_t cpu_vendor;
char cpu_model[64] = "unknown";
uint32_t cpu_id;
uint32_t cpu_procinfo;
uint32_t cpu_feature;
uint32_t cpu_feature2;
uint32_t cpuid_high;
uint32_t cpuid_exthigh;

static void cpu_nmi_handler(__unused int num, __unused trapframe_t *tf,
	__unused void *arg)
{
	if(mp_nmi_handler() == false) {
		kpanic("recieved NMI on CPU%d\n", cur_cpu()->id);
	}
}

static void cpu_get_intr(uint8_t num, cpu_intr_hand_t **handp, void **arg) {
	*arg = cpu_intrs[num].arg;
	atomic_thread_fence(ATOMIC_ACQUIRE);
	*handp = atomic_load_relaxed(&cpu_intrs[num].hand);
}

void cpu_set_intr_handler(uint8_t num, cpu_intr_hand_t *hand, void *arg) {
	kassert(cpu_intrs[num].hand == NULL, "[cpu] multiple INTR%d handlers",
		num);

	/*
	 * Set arg before setting the handler.
	 */
	cpu_intrs[num].arg = arg;
	atomic_store_release(&cpu_intrs[num].hand, hand);
}

/*
 * Called from interrupt.S
 */
void asmlinkage cpu_intr_handler(trapframe_t *regs) {
	thread_t *thread = cur_thread();
	cpu_intr_hand_t *hand;
	void *arg;

	if(thread && TF_USER(regs)) {
		thread->trapframe = regs;
	}

	if(regs->int_no == INT_NMI) {
		for(;;) ;
	}

#if 0
	/*
	 * TODO temporary
	 */
	if(!(regs->eflags & EFL_IF)) {
		kpanic("int: %d eip: 0x%x cr2: 0x%x", regs->int_no, regs->eip,
			cr2_get());
	}
#endif

	cpu_get_intr(regs->int_no, &hand, &arg);
	if(hand) {
		hand(regs->int_no, regs, arg);
	} else {
		kprintf("[cpu] Unhandled interrupt at 0x%x: %d\n", regs->eip,
			regs->int_no);
		kpanic("TODO: %d", regs->int_no);
	}

	if(thread && TF_USER(regs)) {
		/*
		 * uret sets thread->trapframe to NULL again.
		 */
		thread_uret();
	}

	if(!cpu_intr_enabled()) {
		sched_intr_preempt();
	}
}

uint32_t getgs(void) {
	gdt_entry_t *entry = &cur_cpu()->arch.gdt[SEG_GS];
	return (uint32_t) entry->base_lo << 0 | (uint32_t) entry->base_hi << 24;
}

void setgs(uint32_t base) {
	gdt_entry_t *entry = &cur_cpu()->arch.gdt[SEG_GS];
	entry->base_lo = base >> 0 & 0xFFFFFF;
	entry->base_hi = base >> 24 & 0xFF;
}

void cpu_set_kernel_stack(uintptr_t stack) {
	cur_cpu()->arch.tss.esp0 = stack;
}

void __noreturn arch_cpu_idle(void) {
	while(true) {
		/*
		 * Don't hlt if a thread would be ready.
		 */
		if(sched_has_runnable()) {
			schedule();
		}

		kassert(cpu_intr_enabled(), "[idle] interrupts not enabled");
		hlt();
	}
}

static inline void lgdt(gdt_entry_t * p, int size) {
	static volatile uint16_t pd[3];
	pd[0] = size - 1;
	pd[1] = (uintptr_t)p & 0xffff;
	pd[2] = (uintptr_t)p >> 16;
	gdt_flush((uintptr_t) pd);
}

static void cpu_seg_init(arch_cpu_t * c) {
	uint32_t tss = (uint32_t) & c->tss;
	size_t tss_limit = sizeof(tss_entry_t);
	uintptr_t gs, fs;

	c->self = cpu_from_arch(c); /* Needed for cpu-local storage */

	gs = stack_canary_init(&c->canary);
	fs = (uintptr_t) c;

	memset(c->gdt, 0, sizeof(c->gdt));
	segment(&c->gdt[SEG_NULL], 0, 0, 0,  DPL_KERN, 0, 0);
	segment(&c->gdt[SEG_KCODE], 0, 0xFFFFFFFF, 26, DPL_KERN, 1, 1);
	segment(&c->gdt[SEG_KDATA], 0, 0xFFFFFFFF, 18, DPL_KERN, 1, 1);
	segment(&c->gdt[SEG_UCODE], 0, 0xFFFFFFFF, 26, DPL_USER, 1, 1);
	segment(&c->gdt[SEG_UDATA], 0, 0xFFFFFFFF, 18, DPL_USER, 1, 1);
	segment(&c->gdt[SEG_TSS], tss, tss_limit, 9,  DPL_KERN, 0, 0);
	segment(&c->gdt[SEG_GS], 0, 0xFFFFFFFF, 18, DPL_USER, 1, 1);
	segment(&c->gdt[SEG_FS], fs, 0xFFFFFFFF, 18, DPL_KERN, 1, 1);
	segment(&c->gdt[SEG_CANARY], gs, gs + 0x18,  18, DPL_KERN, 1, 1);
	segment(&c->gdt[SEG_CODE16], 0, 0xFFFFF, 27, DPL_KERN, 0, 0);
	segment(&c->gdt[SEG_DATA16], 0, 0xFFFFF, 19, DPL_KERN, 0, 0);

	c->tss.ss0 = KDATA_SEL;
	c->tss.cs = KCODE_SEL;
	c->tss.ss = KDATA_SEL;
	c->tss.ds = KDATA_SEL;
	c->tss.es = KDATA_SEL;
	c->tss.fs = FS_SEL;
	c->tss.gs = CANARY_SEL;
	c->tss.esp0 = 0;

	lgdt(c->gdt, sizeof(c->gdt));
	ltr(SEG_SEL(SEG_TSS, DPL_KERN));
}

static void setidt(int num, void *func, uint16_t cs, uint8_t type,
	uint8_t dpl)
{
	idt_gate_t *gate = &idt_gates[num];

	gate->base_low = (uintptr_t)func & 0xFFFF;
	gate->base_high = ((uintptr_t)func >> 16) & 0xFFFF;
	gate->cs = cs;
	gate->zero = 0;
	gate->type = type;
	gate->s = 0;
	gate->dpl = dpl;
	gate->p = 1;
}

static __init void idt_init(void) {
	size_t i;

	for(i = 0; i < EXCEPTION_NUM; i++) {
		setidt(i, int_vectors[i], KCODE_SEL, IDT_TYPE_TRAP, DPL_KERN);
	}

	for(int i = EXCEPTION_NUM; i < INT_NUM; i++) {
		setidt(i, int_vectors[i], KCODE_SEL, IDT_TYPE_INTR, DPL_KERN);
	}

	/*
	 * Pagefault has to be an interruptand not a trap to protect
	 * the cr2 register when a page fault happens. Interrupts will
	 * be enabled by the pagefault handler once cr2 was read.
	 */
	setidt(INT_PF, int_vectors[INT_PF], KCODE_SEL, IDT_TYPE_INTR, DPL_KERN);
	setidt(INT_SYSCALL, syscall_idt, KCODE_SEL, IDT_TYPE_TRAP, DPL_USER);
	setidt(INT_APIC_SPURIOUS, lapic_spurious, KCODE_SEL, IDT_TYPE_INTR,
		DPL_USER);
}

#if notyet
#include <arch/delay.h>
#endif

static void cpu_detect(void) {
	char vendor[13];
	size_t i;

	cpuid(0, &cpuid_high, (void *)vendor, (void *)vendor + 8,
		(void *)vendor + 4);

	if(!memcmp(vendor, "GenuineIntel", sizeof(vendor))) {
		cpu_vendor = CPU_VEN_INTEL;
	} else {
		vendor[12] = '\0';
		kpanic("[cpu] unknown vendor: %s", vendor);
	}

	cpuid(1, &cpu_id, &cpu_procinfo, &cpu_feature2, &cpu_feature);

	/*
	 * Check whether the cpu has all the feature this kernel needs.
	 */
	if((cpu_feature & CPU_FEAT) != CPU_FEAT) {
		kpanic("[cpu] lack of cpu features: 0x%x", cpu_feature);
	}

	cpuid(0x80000000, &cpuid_exthigh, NULL, NULL, NULL);
	if(cpuid_exthigh >= 0x80000004) {
		for(i = 0; i < 3; i++) {
			void *ptr = cpu_model + i * 16;
			cpuid(0x80000002 + i, ptr, ptr + 4, ptr + 8, ptr + 12);
		}

		cpu_model[48] = '\0';
	}

#if notyet
	/*
	 * Get the TSC frequency.
	 */
	uint64_t tsc1, tsc2;

	early_delay_setup();
	tsc1 = rdtsc();
	early_delay(MILLI2NANO(1));
	tsc2 = rdtsc();

	uint64_t freq = tsc2 - tsc1;
	kprintf("KHZ: %lld\n", freq);
	kprintf("MHZ: %lld\n", freq / 1000);
	kprintf("GHZ: %lld\n", freq / 1000000);
#endif

	kprintf("[cpu] processor information:\n");
	kprintf("\tvendor: %s\n", cpu_vendor_str(cpu_vendor));
	kprintf("\tmodel: %s\n", cpu_model);
}

void arch_cpu_init(arch_cpu_t *cpu) {
	const bool bsp = cpu == &boot_cpu.arch;

	if(bsp) {
		idt_init();
	}

	cpu_seg_init(cpu);
	lidt((uintptr_t)idt_gates, sizeof(idt_gates) - 1);

	if(bsp) {
		cpu_detect();
	}

	fpu_cpu_init();
	kwp_enable();
}

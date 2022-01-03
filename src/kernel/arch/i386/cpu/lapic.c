/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 *
 * Copyright (c) 2018, Elias Zell
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
#include <kern/percpu.h>
#include <kern/init.h>
#include <kern/io.h>
#include <device/evtimer.h>
#include <vm/kern.h>
#include <arch/msr.h>
#include <arch/interrupt.h>
#include <arch/lapic.h>
#include <arch/delay.h>

#define LAPIC_SIZE		0x400
#define LAPIC_ID		0x020 /* ID */
#define LAPIC_VER		0x030 /* Version */
#define LAPIC_TPR		0x080 /* Task Priority */
#define LAPIC_EOI		0x0B0 /* EOI */
#define LAPIC_SVR		0x0F0 /* Spurious Interrupt Vector */
#define 	LAPIC_SVR_ENABLE	(1U << 8)
#define LAPIC_ESR		0x280 /* Error Status */
#define LAPIC_LVT_CMCI 		0x2F0
#define LAPIC_ICRLO		0x300 /* Interrupt Command */
#define 	LAPIC_ICR_VECTOR	((x) & 0xFF)
#define 	LAPIC_ICR_FIXED		(0U << 8)
#define 	LAPIC_ICR_LOWPRI	(1U << 8)
#define		LAPIC_ICR_SMI		(2U << 8)
#define		LAPIC_ICR_NMI		(4U << 8)
#define 	LAPIC_ICR_INIT		(5U << 8)  /* INIT/RESET */
#define 	LAPIC_ICR_STARTUP	(6U << 8)  /* Startup IPI */
#define 	LAPIC_ICR_PHYS		(0U << 11)
#define 	LAPIC_ICR_LOGICAL	(1U << 11)
#define 	LAPIC_ICR_PENDING	(1U << 12) /* Delivery status */
#define 	LAPIC_ICR_ASSERT	(1U << 14)
#define 	LAPIC_ICR_DEASSERT	(0U << 14)
#define 	LAPIC_ICR_LEVEL		(1U << 15)
#define 	LAPIC_ICR_EDGE		(0U << 15)
#define 	LAPIC_DEST_MASK		(3U << 18)
#define			LAPIC_ICR_DESTFIELD	(0U << 18)
#define 		LAPIC_ICR_SELF		(1U << 18)
#define 		LAPIC_ICR_BCAST		(2U << 18)
#define 		LAPIC_ICR_OTHERS	(3U << 18)
#define LAPIC_ICRHI		0x310 /* Interrupt Command [63:32] */
#define 	LAPIC_ICR_DEST_SHIFT	24
#define LAPIC_LVT_TIMER		0x320
#define LAPIC_LVT_THERM		0x330
#define LAPIC_LVT_PERFCNT 	0x340
#define LAPIC_LVT_LINT0		0x350
#define LAPIC_LVT_LINT1		0x360
#define LAPIC_LVT_ERROR		0x370
#define 	LAPIC_LVT_VECTOR	((x) & 0xFF)
#define 	LAPIC_LVT_DM_FIXED	(0U << 8)
#define 	LAPIC_LVT_DM_SMI	(2U << 8)
#define 	LAPIC_LVT_DM_NMI	(4U << 8)
#define 	LAPIC_LVT_INIT		(5U << 8)
#define 	LAPIC_LVT_DM_EXTINT	(7U << 8)
#define		LAPIC_LVT_DS_PENDING	(1U << 12)
#define 	LAPIC_LVT_ACTIVE_LOW	(1U << 13)
#define 	LAPIC_LVT_ACTIVE_HI	(0U << 13)
#define 	LAPIC_LVT_REMOTE_IRR	(1U << 14)
#define 	LAPIC_LVT_EDGE		(0U << 15)
#define 	LAPIC_LVT_LEVEL		(1U << 15)
#define 	LAPIC_LVT_MASKED	(1U << 16)
#define		LAPIC_LVT_ONESHOT	(0U << 17)
#define 	LAPIC_LVT_PERIODIC	(1U << 17)
#define		LAPIC_LVT_TSC_DEADLINE	(2U << 17)
#define LAPIC_TICR		0x380 /* Timer Initial Count */
#define LAPIC_TCCR		0x390 /* Timer Current Count */
#define LAPIC_TDCR		0x3E0 /* Timer Divide Configuration */
#define 	APIC_TDCR_2		0x00
#define 	APIC_TDCR_4		0x01
#define 	APIC_TDCR_8		0x02
#define 	APIC_TDCR_16		0x03
#define 	APIC_TDCR_32		0x08
#define 	APIC_TDCR_64		0x09
#define 	APIC_TDCR_128		0x0a
#define 	APIC_TDCR_1		0x0b
#define	LAPIC_TIMER_MAX	0xffffffff

typedef struct lapic {
	evtimer_t timer;
} lapic_t;

static DEFINE_PERCPU(lapic_t, lapic_pcpu);
static frequency_t lapic_freq;
static uint8_t lapic_div;
static void *lapic_map;

static lapic_t *lapic_percpu(void) {
	return PERCPU(&lapic_pcpu);
}

static inline uint32_t lapic_read(int idx) {
	return readl(lapic_map + idx);
}

static inline void lapic_write(int idx, uint32_t val) {
	writel(lapic_map + idx, val);
}

void lapic_eoi(void) {
	lapic_write(LAPIC_EOI, 0);
}

uint8_t lapic_id(void) {
	return lapic_read(LAPIC_ID) >> 24;
}

static void lapic_handle_error(__unused int intr, __unused struct trapframe *tf,
	__unused void *arg)
{
	uint32_t esr;

	/*
	 * Force the local APIC to update its error register by
	 * writing to it.
	 */
	lapic_write(LAPIC_ESR, 0);
	esr = lapic_read(LAPIC_ESR);

	kprintf("[lapic] cpu%d: error: 0x%x\n", cur_cpu()->id, esr);
	lapic_eoi();
}

static void lapic_handle_timer(__unused int intr, __unused struct trapframe *tf,
	__unused void *arg)
{
	evtimer_intr(&lapic_percpu()->timer);
	lapic_eoi();
}

static void lapic_send_ipi(uint32_t intr, int dest) {
	lapic_write(LAPIC_ICRHI, dest << LAPIC_ICR_DEST_SHIFT);
	lapic_write(LAPIC_ICRLO, intr);
}

int lapic_ipi_wait(size_t loops) {
	for(size_t i = 0; i < loops; i++) {
		cpu_relax();

		if(!(lapic_read(LAPIC_ICRLO) & LAPIC_ICR_PENDING)) {
			return 0;
		}
	}

	return -1;
}

void lapic_ipi(int vec, int dest) {
	uint32_t lo, hi = 0;

	if(vec == INT_NMI_PANIC) {
		lo = LAPIC_ICR_NMI;
	} else {
		lo = vec | LAPIC_ICR_FIXED;
	}

	switch(dest) {
	case LAPIC_IPI_OTHERS:
		lo |= LAPIC_ICR_OTHERS;
		break;
	case LAPIC_IPI_BCAST:
		lo |= LAPIC_ICR_BCAST;
		break;
	default:
		hi = dest;
	}

	lapic_send_ipi(lo, hi);
}

int lapic_start_ap(int id, uint16_t addr) {
	/*
	 * Send INIT (level-triggered) interrupt to reset other CPU
	 */
	lapic_send_ipi(LAPIC_ICR_INIT | LAPIC_ICR_PHYS | LAPIC_ICR_ASSERT |
		LAPIC_ICR_LEVEL | LAPIC_ICR_DESTFIELD, id);

	/*
	 * The 10000 is just an arbitrary value...
	 */
	lapic_ipi_wait(10000);

	/*
	 * Deassert the INIT ipi.
	 */
	lapic_write(LAPIC_ICRLO, LAPIC_ICR_INIT | LAPIC_ICR_PHYS |
		LAPIC_ICR_DEASSERT | LAPIC_ICR_LEVEL | LAPIC_ICR_DESTFIELD);

	/*
	 * Wait approx 10ms.
	 */
	x86_io_mdelay(10);

	for(int i = 0; i < 2; i++) {
		lapic_send_ipi(LAPIC_ICR_STARTUP | LAPIC_ICR_PHYS |
			LAPIC_ICR_ASSERT | LAPIC_ICR_EDGE |
			LAPIC_ICR_DESTFIELD | (addr >> 12), id);

		x86_io_udelay(200);
	}

	return 0;
}

static void lapic_setup_intr(void) {
	/*
	 * Stop the timer and disable various LVT-interrupts.
	 */
	lapic_write(LAPIC_LVT_TIMER, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_LVT_THERM, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_LVT_PERFCNT, LAPIC_LVT_MASKED);

	/*
	 * TODO use MADT for obtaining that info.
	 */
	lapic_write(LAPIC_LVT_LINT0, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_LVT_LINT1, LAPIC_LVT_MASKED);

	/*
	 * Disable performance counter overflow interrupts if present.
	 */
	if(((lapic_read(LAPIC_VER) >> 16) & 0xFF) >= 4) {
		lapic_write(LAPIC_LVT_PERFCNT, LAPIC_LVT_MASKED);
	}

	lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | INT_APIC_SPURIOUS);
	lapic_write(LAPIC_LVT_ERROR, INT_APIC_ERROR);

	/*
	 * Clear error status register.
	 */
	lapic_write(LAPIC_ESR, 0);
	lapic_write(LAPIC_ESR, 0);

	/*
	 * Ack any outstanding interrupts.
	 */
	lapic_eoi();

	/*
	 * Send an Init Level De-Assert to synchronise arbitration ID's.
	 */
	lapic_write(LAPIC_ICRHI, 0);
	lapic_write(LAPIC_ICRLO, LAPIC_ICR_BCAST | LAPIC_ICR_INIT |
		LAPIC_ICR_LEVEL);
	while(lapic_read(LAPIC_ICRLO) & LAPIC_ICR_PENDING) {
		continue;
	}

	/*
	 * Allow all priority levels.
	 */
	lapic_write(LAPIC_TPR, 0);
}

static void lapic_timer_config(__unused evtimer_t *timer, evtimer_mode_t mode,
	uint64_t cntr)
{
	uint32_t lapic_mode;

	lapic_mode = INT_APIC_TIMER | LAPIC_LVT_DM_FIXED | LAPIC_LVT_EDGE |
			LAPIC_LVT_ACTIVE_HI;
	switch(mode) {
	case EV_PERIODIC:
		lapic_mode |= LAPIC_LVT_PERIODIC;
		break;
	case EV_ONESHOT:
		lapic_mode |= LAPIC_LVT_ONESHOT;
		break;
	}

	lapic_write(LAPIC_TDCR, lapic_div);
	lapic_write(LAPIC_LVT_TIMER, lapic_mode);
	lapic_write(LAPIC_TICR, cntr & 0xFFFFFFFF);
}

static void lapic_timer_stop(__unused evtimer_t *timer) {
	lapic_write(LAPIC_TDCR, INT_APIC_TIMER | LAPIC_LVT_DM_FIXED |
		LAPIC_LVT_EDGE | LAPIC_LVT_ACTIVE_HI | LAPIC_LVT_MASKED |
		LAPIC_LVT_ONESHOT);
}

static void lapic_calibrate(void) {
	uint32_t ccr, div = 2;

	early_delay_setup();
	for(lapic_div = APIC_TDCR_2; lapic_div <= APIC_TDCR_128; lapic_div++) {
		lapic_write(LAPIC_TDCR, lapic_div);
		lapic_write(LAPIC_LVT_TIMER, LAPIC_LVT_ONESHOT |
			LAPIC_LVT_MASKED | INT_APIC_TIMER | LAPIC_LVT_DM_FIXED |
			LAPIC_LVT_EDGE | LAPIC_LVT_ACTIVE_HI);

		lapic_write(LAPIC_TICR, LAPIC_TIMER_MAX);

		early_delay(MILLI2NANO(100));
		ccr = LAPIC_TIMER_MAX - lapic_read(LAPIC_TCCR);
		if(ccr != LAPIC_TIMER_MAX) {
			break;
		}

		div <<= 1;
	}

	/*
	 * We only delayed 100 milliseconds, thus we have to multiply
	 * the value by ten, to get the value HZ.
	 */
	ccr *= 10;

	kprintf("[lapic] divisor: %d, frequency: %d Hz\n", div, ccr);
	lapic_freq = ccr;
}

static void lapic_evtimer_init(void) {
	lapic_t *lapic = lapic_percpu();

	lapic->timer.name = "lapic";
	lapic->timer.min_period = (0x00000002U * SEC_NANOSECS) / lapic_freq;
	lapic->timer.max_period = (0xfffffffeU * SEC_NANOSECS) / lapic_freq;
	lapic->timer.freq = lapic_freq;
	lapic->timer.priv = lapic;
	lapic->timer.cpu = cur_cpu()->id;
	lapic->timer.flags = EV_F_PERIODIC | EV_F_ONESHOT;
	lapic->timer.config = lapic_timer_config;
	lapic->timer.stop = lapic_timer_stop;
	evtimer_register(&lapic->timer);
}

static void lapic_check_id(void) {
	kassert(cur_cpu()->id == lapic_id(), "[lapic] lapic id mismatch: "
		"detected: %d, actual: %d", cur_cpu()->id, lapic_id());
}

void lapic_init(void)  {
	lapic_check_id();
	lapic_setup_intr();
	lapic_evtimer_init();
}

static __init int lapic_boot_init(void) {
	uint32_t base;

	base = rdmsr32(MSR_IA32_APIC_BASE);
	if(!(base & MSR_IA32_APIC_BASE_ENABLE)) {
		kpanic("[lapic] lapic is not enabled");
	}

	base &= MSR_IA32_APIC_BASE_BASE_MASK;
	lapic_map = vm_mapdev(base, LAPIC_SIZE, VM_MEMATTR_UNCACHEABLE);
	cpu_set_intr_handler(INT_APIC_ERROR, lapic_handle_error, NULL);
	cpu_set_intr_handler(INT_APIC_TIMER, lapic_handle_timer, NULL);

	lapic_check_id();
	lapic_setup_intr();
	lapic_calibrate();
	lapic_evtimer_init();

	return INIT_OK;
}

early_initcall(lapic_boot_init);

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
#include <kern/atomic.h>
#include <kern/cpu.h>
#include <kern/init.h>
#include <kern/proc.h>
#include <kern/sched.h>
#include <kern/timer.h>
#include <kern/mp.h>
#include <kern/sync.h>
#include <kern/main.h>
#include <kern/critical.h>
#include <lib/unaligned.h>
#include <lib/string.h>
#include <arch/lapic.h>
#include <arch/x86.h>
#include <vm/layout.h>
#include <vm/vas.h>
#include <vm/mmu.h>
#include <arch/interrupt.h>

static __initdata bool mp_startup_done = false;
static sync_t ipi_lock = SYNC_INIT(SPINLOCK);
static vm_vaddr_t ipi_inval_addr;
static vm_vsize_t ipi_inval_size;
static mmu_ctx_t *ipi_inval_ctx;
static size_t ipi_done;
static bool mp_panic = false;

void ipi_invlpg(mmu_ctx_t *ctx, vm_vaddr_t addr, vm_vsize_t size) {
	size_t target = cpu_num() - 1;

	assert(size);
	if(ipi_enabled) {
		sync_scope_acquire(&ipi_lock);
		ipi_done = 0;
		ipi_inval_addr = addr;
		ipi_inval_size = size;
		ipi_inval_ctx = ctx;
		lapic_ipi(INT_IPI_INVLPG, LAPIC_IPI_OTHERS);

		while(atomic_load_relaxed(&ipi_done) < target) {
			cpu_relax();
		}

		ipi_done = 0;
	}
}

static void ipi_invlpg_handler(__unused int intr, __unused struct trapframe *tf,
	__unused void *arg)
{
	if(ipi_inval_ctx == mmu_cur_ctx || ipi_inval_ctx == mmu_kern_ctx ||
		VM_IS_KERN(ipi_inval_addr))
	{
		for(size_t i = 0; i < ipi_inval_size; i += PAGE_SZ) {
			invlpg(ipi_inval_addr + i);
		}
	}

	atomic_inc_relaxed(&ipi_done);
	lapic_eoi();
}

void ipi_panic(void) {
	if(ipi_enabled) {
		/*
		 * Force the other processors into panic mode by sending
		 * a non-maskable-interrupt to them.
		 */
		atomic_store(&mp_panic, true);
		lapic_ipi(INT_NMI_PANIC, LAPIC_IPI_OTHERS);
	}
}

bool mp_nmi_handler(void) {
	if(atomic_load_relaxed(&mp_panic)) {
		/*
		 * The EOI useless, I know.
		 */
		lapic_eoi();
		while(true) {
			cpu_relax();
		}

		return true;
	} else {
		return false;
	}
}

static void arch_ipi_bitmap_handler(__unused int intr,
	__unused struct trapframe *tf, __unused void *arg)
{
	ipi_bitmap_handler();
	lapic_eoi();
}

void ipi_bitmap_send(cpu_t *cpu) {
	lapic_ipi(INT_IPI_BITMAP, cpu->id);
	if(lapic_ipi_wait(100000) == -1) {
		kpanic("[mp] timeout while sending bitmap IPI");
	}
}

void __init __noreturn asmlinkage arch_ap_main(cpu_t *cpu) {
	arch_cpu_init(&cpu->arch);
	lapic_init();
	init_timer();
	sched_init_ap(cpu);
	atomic_store(&cpu->running, true);

	/*
	 * Wait until all the processors are started up.
	 */
	while(atomic_load_relaxed(&mp_startup_done) == false) {
		cpu_relax();
	}

	/*
	 * The boot cpu has unmapped the mapping used for
	 * the startup trampoline.
	 */
	invltlb();
	ap_main();
}

#include <device/nvram.h> /* TODO */

void __init arch_mp_init(void) {
	uint16_t *const wrv = (void *)0x467 + KERNEL_VM_BASE;
	const size_t code_sz = ap_entry_end - ap_entry_start;
	uint32_t *code = (void *)AP_CODE_ADDR;
	uintptr_t stack;
	cpu_t *cpu;

	cpu_set_intr_handler(INT_IPI_BITMAP, arch_ipi_bitmap_handler, NULL);
	cpu_set_intr_handler(INT_IPI_INVLPG, ipi_invlpg_handler, NULL);

	kprintf("[cpu] launching application processors\n");

	/*
	 * Set the "Shutdown code" to 0xA0 (warm reset)
	 */
	nvram_t *cmos = nvram_search("cmos");
	assert(cmos != NULL);
	nvram_writeb(cmos, 0xF, 0x0A);
	nvram_put(cmos);

	/*
	 * Warm-reset vector
	 */
	unaligned_write16(wrv + 0, 0);
	unaligned_write16(wrv + 1, AP_CODE_ADDR >> 4);

	/*
	 * Make sure that the cpu can still run once paging is enabled by
	 * identity mapping the ap trampoline.
	 */
	assert(AP_CODE_ADDR < LPAGE_SZ);
	mmu_map_ap();

	/*
	 * Copy the ap trampoline in the correct place.
	 */
	memcpy(code, (void *)((uintptr_t)ap_entry_start + KERNEL_VM_BASE), code_sz);

	/*
	 * Allocate the necessery memory needed for starting the
	 * AP processors. This has to be done before entering
	 * the critical section.
	 */
	foreach_ap(cpu) {
		assert(cpu != &boot_cpu);
		cpu->boot_thr = kthread_alloc(NULL, NULL);
	}

	/*
	 * It is not impossible that there is a concurrent thread running
	 * at this point in time. Maybe the async-thread needs to currently
	 * cleanup a device interrupt thread, which was freed because
	 * the device was not present.
	 * If such a thread would be allowed to run concurrently, while
	 * starting the AP processors, we might run into some serious
	 * problems, because these threads might hold a mutex, which
	 * arch_ap_main may also want to acquire and since the scheduler
	 * of the AP cpu might not yet be initialized during this time,
	 * erros may occur.
	 */
	critical_enter();
	foreach_ap(cpu) {
		stack = (uintptr_t)cpu->boot_thr->kstack + THREAD_KSTACK -
			16 + 8;

		/*s
		 * Give the trampoline code some necessary informaton.
		 */
		ap_tramp_arg(AP_ARG_PGDIR, mmu_kern_ctx->cr3);
		ap_tramp_arg(AP_ARG_STACK, stack);
		ap_tramp_arg(AP_ARG_KMAIN, (uintptr_t)arch_ap_main);
		ap_tramp_arg(AP_ARG_CPU, (uintptr_t)cpu);
		kprintf("[cpu] launching cpu %d\n", cpu->id);

		/*
		 * Launch the processor by sending appropriate IPIs.
		 */
		lapic_start_ap(cpu->id, AP_CODE_ADDR);
		while(atomic_load_relaxed(&cpu->running) == false) {
			cpu_relax();
		}

		kprintf("[cpu] launched cpu %d\n", cpu->id);
	}

	critical_leave();
	mmu_unmap_ap();
	atomic_store(&ipi_enabled, true);
	atomic_store(&mp_startup_done, true);
}

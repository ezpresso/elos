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
#include <kern/percpu.h>
#include <kern/sched.h>
#include <kern/mp.h>
#include <kern/env.h>
#include <kern/init.h>
#include <kern/cpu.h>

#define IPI_PREEMPT	(1 << 0)
#define IPI_INTR	(1 << 1)

typedef struct mp_percpu {
	uint8_t ipi_pending;
} mp_percpu_t;

static KERN_ENV_BOOL(mp_on, "kern.mp", true);
static DEFINE_PERCPU(mp_percpu_t, mp_pcpu);
static __initdata size_t mp_ncpu_done = 1;
bool ipi_enabled = false;

static inline void ipi_bitmap(cpu_t *cpu, uint8_t bits) {
	mp_percpu_t *pcpu = PERCPU_CPU(cpu, &mp_pcpu);
	uint8_t old, new;

	if(!ipi_enabled) {
		return;
	}

	/*
	 * Only send one ipi at a time to avoid redundant ipis.
	 */
	do {
		old = atomic_load_relaxed(&pcpu->ipi_pending);
		new = old | bits;
	} while(!atomic_cmpxchg(&pcpu->ipi_pending, old, new));

	if(old == 0) {
		ipi_bitmap_send(cpu);
	}
}

void ipi_preempt(cpu_t *cpu) {
	ipi_bitmap(cpu, IPI_PREEMPT);
}

void ipi_intr(cpu_t *cpu) {
	ipi_bitmap(cpu, IPI_INTR);
}

void ipi_bitmap_handler(void) {
	mp_percpu_t *pcpu = PERCPU(&mp_pcpu);
	uint8_t ipi;

	ipi = atomic_xchg_relaxed(&pcpu->ipi_pending, 0);
	if(ipi & IPI_PREEMPT) {
		schedule_async();
	}
}

void mp_ap_wait(void) {
	atomic_inc_relaxed(&mp_ncpu_done);
	while(atomic_load_relaxed(&mp_ncpu_done) != 0) {
		cpu_relax();
	}
}

bool mp_capable(void) {
	return kern_var_getb(&mp_on) == true && cpu_num() > 1;
}

void __init init_mp(void) {
	kern_var_lock(&mp_on);
	arch_mp_init();

	while(atomic_load_relaxed(&mp_ncpu_done) != cpu_num()) {
		cpu_relax();
	}

	kprintf("[mp] every cpu is running now\n");
	atomic_store(&mp_ncpu_done, 0);
}

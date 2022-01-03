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
#include <kern/wait.h>
#include <kern/init.h>
#include <kern/sched.h>
#include <vm/pressure.h>

#define VM_PR_LOW_PCT	50 /* 50-100% free => low memory pressure */
#define VM_PR_MOD_PCT	30 /* 30- 50% free => moderate memory pressure */
#define VM_PR_HIGH_PCT	0  /* 0 - 30% free => high memory pressure */

typedef struct vm_pr_mem {
	waitqueue_t waitq;
	vm_pressure_t pr;
	uint64_t total;
	uint64_t free;
	uint64_t threshold;
} vm_pr_mem_t;

typedef struct vm_pr_wait {
	waiter_t waiter;
	vm_pr_flags_t flags;
	vm_pressure_t pr;
} vm_pr_wait_t;

static vm_pr_mem_t vm_pr_mem[VM_PR_MEM_NUM];
static sync_t vm_pr_lock = SYNC_INIT(MUTEX);
static DEFINE_WAITQUEUE(vm_pr_wq);

static inline vm_pressure_t vm_calc_pressure(uint64_t total, uint64_t free) {
	uint64_t div;

	div = (free * 100) / total;
	if(div >= VM_PR_LOW_PCT) {
		return VM_PR_LOW;
	} else if(div >= VM_PR_MOD_PCT) {
		return VM_PR_MODERATE;
	} else {
		return VM_PR_HIGH;
	}
}

static inline bool vm_pr_wait_done(vm_pr_mem_t *mem, vm_pr_wait_t *wait) {
	return ((1 << (mem - vm_pr_mem)) & wait->flags) && mem->pr >= wait->pr;
}

void vm_pressure_wait(vm_pr_flags_t flags, vm_pressure_t pr) {
	vm_pr_wait_t wait;
	size_t i;

	kassert(flags, NULL);
	kassert(pr > VM_PR_LOW, NULL);

	wait.flags = flags;
	wait.pr = pr;

	synchronized(&vm_pr_lock) {
		for(i = 0; i < VM_PR_MEM_NUM; i++) {
			if(vm_pr_wait_done(&vm_pr_mem[i], &wait)) {
				return;
			}
		}

		waiter_init(&wait.waiter);
		wait_prep(&vm_pr_wq, &wait.waiter);
	}

	wait_sleep(&vm_pr_wq, &wait.waiter, 0);
	waiter_destroy(&wait.waiter);
}

void vm_pressure_add(vm_pr_mem_type_t type, int64_t free) {
	vm_pr_mem_t *mem = &vm_pr_mem[type];
	waiter_t *waiter;
	vm_pressure_t pr;
	bool incr;

	/*
	 * TODO make this nicer
	 */
	if(free < 0) {
		kassert(mem->free >= (uint64_t)-free, NULL);
	} else {
		kassert(mem->free + free <= mem->total, NULL);
	}

	/*
	 * mem->free can be read without locking in this case, because
	 * vmem_lock of vm_phylock is held by the caller.
	 */
	pr = vm_calc_pressure(mem->total, mem->free + free);
	incr = pr > mem->pr;

	synchronized(&vm_pr_lock) {
		mem->free += free;
		mem->pr = pr;
	}

	/*
	 * We only need to wakeup pressure waiters if the pressure increases.
	 */
	if(incr) {
		waitq_foreach(waiter, &vm_pr_wq) {
			if(vm_pr_wait_done(mem, (vm_pr_wait_t *) waiter)) {
				wakeup_waiter(&vm_pr_wq, waiter, SCHED_KERNEL);
			}
		}
	} else if(mem->free >= mem->threshold) {
		/*
		 * If the pressure decreases  wakeup the ones waiting for
		 * memory to become available.
		 *
		 * TODO
		 */
		wakeup(&mem->waitq, SCHED_NORMAL);
	}
}

uint64_t vm_mem_get_free(vm_pr_mem_type_t type) {
	sync_scope_acquire(&vm_pr_lock);
	return vm_pr_mem[type].free;
}

bool vm_mem_wait_p(vm_pr_mem_type_t type, uint64_t size) {
	vm_pr_mem_t *mem = &vm_pr_mem[type];

	/*
	 * The caller holds vmem_lock or vm_phylock, so it's safe to read
	 * mem->free without vm_pr_lock.
	 */
	return mem->free < (size + mem->threshold);
}

void vm_mem_wait(vm_pr_mem_type_t type, uint64_t size) {
	vm_pr_mem_t *mem = &vm_pr_mem[type];
	waiter_t wait;

	waiter_init(&wait);

	synchronized(&vm_pr_lock) {
		while(vm_mem_wait_p(type, size)) {
			wait_prep(&mem->waitq, &wait);
			sync_release(&vm_pr_lock);
			wait_sleep(&mem->waitq, &wait, 0);
			sync_acquire(&vm_pr_lock);
		}
	}

	waiter_destroy(&wait);
}

void vm_mem_wait_free(vm_pr_mem_type_t type, sync_t *lock) {
	vm_pr_mem_t *mem = &vm_pr_mem[type];
	waiter_t wait;

	wait_init_prep(&mem->waitq, &wait);
	sync_release(lock);
	wait_sleep(&mem->waitq, &wait, 0);
	sync_acquire(lock);
	waiter_destroy(&wait);
}

vm_pressure_t vm_pressure(vm_pr_flags_t flags) {
	vm_pressure_t prmax = VM_PR_LOW;

	for(vm_pr_mem_type_t i = 0; i < VM_PR_MEM_NUM; i++) {
		if(flags & (1 << i)) {
			sync_scope_acquire(&vm_pr_lock);
			prmax = max(prmax, vm_calc_pressure(vm_pr_mem[i].total,
				vm_pr_mem[i].free));
		}
	}

	return prmax;
}

void __init vm_pr_mem_init(vm_pr_mem_type_t type, uint64_t total,
	uint64_t free)
{
	waitqueue_init(&vm_pr_mem[type].waitq);
	vm_pr_mem[type].pr = vm_calc_pressure(total, free);
	vm_pr_mem[type].total = total;
	vm_pr_mem[type].free = free;

	/* TODO */
	vm_pr_mem[type].threshold = 0;
}

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
#include <kern/sched.h>
#include <kern/sync.h>
#include <kern/init.h>
#include <kern/time.h>
#include <kern/timer.h>
#include <kern/proc.h>
#include <kern/cpu.h>
#include <kern/percpu.h>
#include <kern/critical.h>
#include <kern/sync.h>
#include <kern/symbol.h>
#include <kern/async.h>
#include <kern/mp.h>
#include <lib/list.h>
#include <vm/vas.h>

typedef struct scheduler {
	sync_t lock;

	/*
	 * only used by current scheduler, not protected.
	 */
#define SCHED_NEEDED (1 << 0)
	int flags;

	struct thread *exit_thread;

	struct thread *thread; /* can be considered constant
				* from the thread's point of view
				*/
	struct thread *idle; /* constant */
	struct cpu *cpu; /* constant */

	/*
	 * Protected by lock.
	 */
	size_t nthread;

	list_t runq[SCHEDQ_NLIST];
	list_t runq_intr;
	size_t runq_ptr;
	uint32_t not_empty; /* bitset of non empty lists */

	timer_t timer;
	bool timer_on;

#if 0
	bool event; /* TMP */
	clock_event_t clock;
#endif
} scheduler_t;

static DEFINE_PERCPU(scheduler_t, scheduler);

static inline scheduler_t *cur_sched(void) {
	return PERCPU(&scheduler);
}

thread_t *cur_thread(void) {
	/*
	 * TODO this is inacceptible:
	 * (it's currently ok, because only one cpu).
	 * consider this:
	 * thread = (&cur_cpu()->scheduler).thread
	 *
	 * This might happen:
	 *
	 * Step 1: cpu = cur_cpu();
	 *      2: sched = PERCPU_CPU(cpu, &scheduler)
	 *		3: the thread is preempted here (due to IRQ or ...)
	 *		4: the thread gets moved to another processor
	 *		5: the thread resumes
	 *		6: sched->thread is not the current thread.
	 *
	 * arch/i386/include/cpu.h solves this issue by atomically
	 * reading %fs:(OFFSET OF arch_cpu_t.thread)
	 *
	 * arch_cpu_t.thread will simply be boot_thr during early init
	 */
	thread_t *thr = cur_sched()->thread;
	if(thr == NULL) {
		return cur_cpu()->boot_thr;
	} else {
		return thr;
	}
}
export(cur_thread);

/**
 * @brief The entry point of idle threads.
 */
static int __noreturn idle(__unused void *arg) {
	arch_cpu_idle();
}

static inline void sched_timer_start(scheduler_t *sched) {
	assert(sched == cur_sched());
	if(sched->timer_on == false) {
		sched->timer_on = true;
		timer_ontick(&sched->timer);
	}
}

/**
 * @brief Choose the next thread to run.
 */
static thread_t *sched_choose(scheduler_t *sched) {
	thread_t *thread;
	size_t ptr;

	sync_assert(&sched->lock);
	if(!sched->nthread) {
		return sched->idle;
	}

	/*
	 * One thread is being removed from the run queue.
	 */
	sched->nthread--;

	/*
	 * Interrupt threads are most important.
	 */
	thread = list_pop_front(&sched->runq_intr);
	if(thread) {
		goto out;
	}

	/*
	 * There has to be at least one thread in the runq.
	 */
	assert(sched->not_empty != 0);

	/*
	 * Find the next non empty queue.
	 */
	ptr = ffs(sched->not_empty >> sched->runq_ptr);
	if(ptr != 0) {
		ptr += sched->runq_ptr;
	} else {
		/*
		 * Search the first bit being set, starting at the
		 * beginning.
		 */
		ptr = ffs(sched->not_empty);
		assert(ptr > 0);
	}

	/*
	 * ffs returns index+1.
	 */
	ptr--;

	/*
	 * Remove first entry of the queue.
	 */
	thread = list_pop_front(&sched->runq[ptr]);
	if(list_is_empty(&sched->runq[ptr])) {
		bclr(&sched->not_empty, ptr);
	}

	sched->runq_ptr = ptr;


out:
	assert(thread);
	thread->runq_idx = UINT8_MAX;

	assert(thread->state != THREAD_EXIT);
	thread->state = THREAD_RUNNING;
	return thread;
}

/**
 * @brief Add a thread to a scheduler.
 */
static void scheduler_add_thread(scheduler_t *sched, thread_t *thread,
	sched_prio_t prio)
{
	uint8_t off = 0;
	size_t ptr;

	sync_assert(&sched->lock);
	assert(thread->state != THREAD_EXIT);

	thread->sched = sched;
	thread->sched_prio = prio;
	if(thread->state != THREAD_SPAWNED) {
		thread->state = THREAD_RUNNABLE;
	}

	sched->nthread++;
	ptr = sched->runq_ptr;

	/*
	 * This allows the system to be kinda responsive even under
	 * load.
	 */
	switch(prio) {
	case SCHED_INTR:
		/*
		 * SCHED_INTR is a special case.
		 */
		list_append(&sched->runq_intr, &thread->sched_node);
		return;
	case SCHED_IO:
		/*
		 * Insert at the front of the current list.
		 */
		list_add(&sched->runq[ptr], &thread->sched_node);
		goto out;
	case SCHED_INPUT:
		off++;
		/* FALLTHROUGH */
	case SCHED_SIGNAL:
		off++;
		/* FALLTHROUGH */
	case SCHED_KERNEL:
		off++;
		/* FALLTHROUGH */
	case SCHED_LOCK:
		off++;
		break;
	case SCHED_NORMAL:
		/*
		 * Normal threads are put at the end.
		 */
		off = SCHEDQ_NLIST - 1;
		break;
	default:
		kpanic("[sched] invalid priority for queue_insert: %d", prio);
	}


	ptr = (ptr + off) % SCHEDQ_NLIST;
	list_append(&sched->runq[ptr], &thread->sched_node);

out:
	thread->runq_idx = ptr;
	bset(&sched->not_empty, ptr);
}

#if 0
static void scheduler_remove(scheduler_t *sched, thread_t *thread) {
	sync_assert(&sched->lock);

	if(thread->state != THREAD_RUNNING) {
		if(thread->sched_prio == SCHED_INTR) {
			list_remove(&sched->runq_intr, &thread->sched_node);
		} else {
			size_t idx = thread->runq_idx;

			list_remove(&sched->runq[idx], &thread->sched_node);
			if(list_is_empty(&sched->runq[idx])) {
				bclr(&sched->not_empty, idx);
			}

			thread->runq_idx = UINT8_MAX;
		}

		sched->nthread--;
	}
}
#endif

void sched_add_thread(thread_t *thread) {
	scheduler_t *best = NULL;
	bool ipi = false;
	cpu_t *cur;

	thread->runq_idx = UINT8_MAX;

	/*
	 * Choose the cpu with the smallest number of threads for thread on.
	 */
	foreach_cpu(cur) {
		if(cur->running) {
			scheduler_t *sched = PERCPU_CPU(cur, &scheduler);
			if(!best || sched->nthread < best->nthread) {
				best = sched;
			}
		}
	}

	assert(best);
	synchronized(&best->lock) {
		scheduler_add_thread(best, thread, thread->prio);
		if(best->timer_on == false) {
			if(best == cur_sched()) {
				sched_timer_start(best);
			} else {
				ipi = true;
			}
		}
	}

	if(ipi) {
		ipi_preempt(best->cpu);
	}
}

bool sched_has_runnable(void) {
	scheduler_t *sched = cur_sched();

	sync_scope_acquire(&sched->lock);
	return sched->nthread != 0;
}
export(sched_has_runnable);

void sched_set_inactive(bool intr) {
	scheduler_t *sched = cur_sched();
	thread_t *thread = sched->thread;

	sync_scope_acquire(&sched->lock);
	assert(!F_ISSET(thread->sflags, THREAD_INTERRUPTABLE));
	if(intr) {
		if(F_ISSET(thread->sflags, THREAD_INTERRUPTED)) {
			return;
		} else {
			F_SET(thread->sflags, THREAD_INTERRUPTABLE);
		}
	}

	F_SET(thread->sflags, THREAD_DO_SLEEP);
}
export(sched_set_inactive);

static bool sched_wakeup_thread(thread_t *thread, sched_prio_t prio) {
	scheduler_t *sched = thread->sched, *this_sched = cur_sched();
	bool ipi = false;

	assert(thread->state != THREAD_EXIT);
	sync_assert(&sched->lock);

	if(thread->state == THREAD_RUNNING) {
		/*
		 * If the thread is running, clear the sleep flag, to stop the
		 * sleep, which the thread currently prepares.
		 */
		thread->sflags &= ~THREAD_DO_SLEEP;
	} else if(thread->state == THREAD_SLEEP) {
		scheduler_add_thread(sched, thread, min(prio, thread->prio));

		if(prio == SCHED_INTR &&
			sched->thread->sched_prio != SCHED_INTR)
		{
			if(thread->sched == this_sched) {
				schedule_async();
			} else {
				ipi = true;
			}
		} else if(sched->timer_on == false) {
			if(sched == this_sched) {
#if 0
				/*
				 * TODO
				 * cannnot call sched_timer_start, because
				 * the time-queue might be locked during a call
				 * to sched_wakeup() by the wait.c interface.
				 */
				sched_timer_start(sched);
#else
				schedule_async();
#endif
			} else {
				ipi = true;
			}
		}
	}

	thread->sflags &= ~THREAD_INTERRUPTABLE;
	return ipi;
}

void sched_wakeup(thread_t *thread, sched_prio_t prio) {
	scheduler_t *sched = thread->sched;
	bool ipi = false;

	synchronized(&sched->lock) {
		ipi = sched_wakeup_thread(thread, prio);
	}

	if(ipi) {
		ipi_preempt(sched->cpu);
	}
}
export(sched_wakeup);

int sched_pending_intr(void) {
	scheduler_t *sched = cur_sched();
	thread_t *thread = sched->thread;
	int intr = 0;

	if(thread->sflags & THREAD_INTERRUPTED) {
		sync_scope_acquire(&sched->lock);

		thread->sflags &= ~(THREAD_INTERRUPTED | THREAD_RESTARTSYS);
		intr = thread->intr;
		thread->intr = 0;
	}

	return intr;
}

void sched_interrupt(thread_t *thread, sched_prio_t prio, int intr, int flags) {
	scheduler_t *sched = thread->sched;
	bool ipi = false;

	synchronized(&sched->lock) {
		if(!(thread->sflags & THREAD_INTERRUPTED) &&
			(thread->sflags & THREAD_INTERRUPTABLE))
		{
			ipi = sched_wakeup_thread(thread, prio);
		}

		thread->intr |= intr;
		thread->sflags |= flags | THREAD_INTERRUPTED;
	}

	if(ipi) {
		ipi_preempt(sched->cpu);
	}
}
export(sched_interrupt);

int sched_interrupted(void) {
	thread_t *thread = cur_thread();
	scheduler_t *sched = thread->sched;
	int flags;

	sync_scope_acquire(&sched->lock);
	flags = thread->sflags;

	/*
	 * sched_pending_intr will clear the THREAD_INTERRUPTED flag later.
	 */
	if(F_ISSET(flags, THREAD_INTERRUPTED)) {
		assert(!F_ISSET(thread->sflags, THREAD_INTERRUPTABLE));
		if(F_ISSET(flags, THREAD_RESTARTSYS)) {
			return -ERESTART;
		} else {
			return -EINTR;
		}
	} else {
		return 0;
	}
}

static inline void sched_free_thread(void *thr) {
	thread_free(thr);
}

static inline void sched_exit_free(scheduler_t *sched) {
	thread_t *exit_thr = sched->exit_thread;
	if(exit_thr) {
		async_t *async;

		/*
		 * Now that the thread, which exited, is no longer running,
		 * it can be freed. Since it would not be safe to call kfree
		 * from the current context, thread_free is called
		 * asynchronously. Since the stack is not being accessed
		 * anymore, it is used to store the async structure.
		 */
		async = exit_thr->kstack;
		async_call(async, sched_free_thread, exit_thr);
		sched->exit_thread = NULL;
	}
}

void sched_postsched(void) {
	sched_exit_free(cur_sched());
	cpu_intr_set(true);
}

static void do_schedule(scheduler_t *sched) {
	/*
	 * keep in mind that last and new change during context switch
	 * because stacks are switched.
	 */
	thread_t *last = sched->thread;

	if(!sched->thread) {
		return;
	}

	synchronized(&sched->lock) {
		if(last != sched->idle && last->state != THREAD_EXIT) {
			/*
			 * Don't put the thread back on the scheduling queue if
			 * the THREAD_DO_SLEEP flag was set.
			 */
			if(F_ISSET(last->sflags, THREAD_DO_SLEEP)) {
				F_CLR(last->sflags, THREAD_DO_SLEEP);
				last->state = THREAD_SLEEP;
			} else {
				/*
				 * Add current thread back on the queue.
				 */
				scheduler_add_thread(sched, last, last->prio);
			}
		}

		/*
		 * Choose a new thread.
		 */
		sched->thread = sched_choose(sched);
	}

	if(sched->thread == sched->idle) {
		if(sched->timer_on) {
			sched->timer_on = false;
			timer_stop(&sched->timer);
		}
	} else {
		/*
		 * TODO do not add the event if there is just
		 * one thread on this cpu.
		 */
		sched_timer_start(sched);
	}

	if(sched->thread == last) {
		assert(last->state != THREAD_EXIT);
		return;
	}

#if 0
	kprintf("%d: schedule: (0x%p) 0x%p %d -> 0x%p %d\n",
		cur_cpu()->id, sched->idle, last, last->tid, sched->thread,
		sched->thread->tid);
#endif

	if(last->state == THREAD_EXIT) {
		assert(!sched->exit_thread);

		/*
		 * A thread cannot be removed while running.
		 * -> sched_postsched() will actually take
		 * care of everything
		 */
		sched->exit_thread = last;
	}

	/*
	 * Sometimes a switch is not needed, however not switching when
	 * a thread exits could be fatal, because the vm_vas might
	 * be freed then if the thread-exit causes a process exit.
	 */
	if((sched->thread->proc != last->proc &&
		sched->thread->proc != &kernel_proc) ||
		(last->state == THREAD_EXIT && last->proc != &kernel_proc))
	{
		vm_vas_switch(sched->thread->proc->vas);
	}

	arch_thread_switch(sched->thread, last);
}

void schedule(void) {
	scheduler_t *sched = cur_sched();

	assert(cpu_intr_enabled());
	cpu_intr_set(false);
	do_schedule(sched);
	sched_exit_free(sched);
	cpu_intr_set(true);
}
export(schedule);

void schedule_async(void) {
	assert(critsect_p() || !cpu_intr_enabled());
	cur_sched()->flags |= SCHED_NEEDED;
}
export(schedule_async);

static bool resched_needed(scheduler_t *sched) {
	int flags;

	flags = sched->flags;
	F_CLR(sched->flags, SCHED_NEEDED);
	return !!(flags & SCHED_NEEDED);
}

void sched_intr_preempt(void) {
	scheduler_t *sched = cur_sched();
	cpu_t *cpu = sched->cpu;

	/*
	 * This scheduler is not running yet.
	 */
	if(cpu == NULL) {
		return;
	}

	assert(!cpu_intr_enabled());
	while(resched_needed(sched)) {
		/*
		 * Interrupts will be reenabled once the thread switch
		 * happened. This makes sure that no further IRQs run
		 * on this stack until the current interrupt context is
		 * popped of the stack again. (I had some cases where
		 * I had too many IRQ contexts on the stack, because
		 * interrupts were reenabled after the IRQ-ack to allow
		 * preemption on timer IRQ, which resulted in a random
		 * stack overflow.)
		 */
		do_schedule(sched);
		sched_exit_free(sched);
		assert(!cpu_intr_enabled());
	}
}

bool sched_need_resched(void) {
	return resched_needed(cur_sched());
}

static void sched_tick(void *arg) {
	assert_critsect("[schedule] tick needs to be called inside critsect");
	assert(arg == cur_sched());
	schedule_async();
}

static void scheduler_init(scheduler_t *sched, cpu_t *cpu) {
	/*
	 * Spawn the idle thread.
	 */
	sched->idle = kthread_alloc(idle, NULL);
	thread_set_flag(sched->idle, THREAD_IDLE);

	sched->cpu = cpu;
	sched->thread = cur_cpu()->boot_thr;
	sched->thread->sched = sched;
	sched->nthread = 0; /* boot_thread & idle don't count */
	sync_init(&sched->lock, SYNC_SPINLOCK);

	/*
	 * Initialize the runq.
	 */
	for(int i = 0; i < SCHEDQ_NLIST; i++) {
		list_init(&sched->runq[i]);
	}

	list_init(&sched->runq_intr);
	sched->runq_ptr = 0;
	sched->timer_on = false;
	timer_init(&sched->timer, sched_tick, sched);
}

void __init init_sched(void) {
	scheduler_t *sched = cur_sched();
	scheduler_init(sched, cur_cpu());
	sched_timer_start(sched);
}

/*
 * Called on each AP processor
 */
void sched_init_ap(cpu_t *cpu) {
	scheduler_init(cur_sched(), cpu);
}

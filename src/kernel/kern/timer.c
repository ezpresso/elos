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
#include <kern/init.h>
#include <kern/timer.h>
#include <kern/time.h>
#include <kern/percpu.h>
#include <kern/sync.h>
#include <device/evtimer.h>

#if 0
#define timer_debug kprintf
#else
#define timer_debug(a...)
#endif

typedef struct timerq {
	sync_t lock;
	evtimer_t *dev;
	list_t queue;
	list_t ontick;
	bool bsp;
} timerq_t;

static DEFINE_PERCPU(timerq_t, timerq_cpu);

static inline timerq_t *timerq_get(void) {
	return PERCPU(&timerq_cpu);
}

static inline bool timer_ontick_p(timer_t *timer) {
	return !!(timer->flags & TIMER_ONTICK);
}

static void timerq_insert(timerq_t *timerq, timer_t *timer) {
	timer_t *cur;

	if(timer_ontick_p(timer)) {
		list_append(&timerq->ontick, &timer->node);
		if(!timerq->bsp && list_length(&timerq->ontick) == 1) {
			evtimer_config(timerq->dev, EV_PERIODIC, TICK_PERIOD);
		}
	} else {
		foreach(cur, &timerq->queue) {
			if(cur->time >= timer->time) {
				list_insert_before(&timerq->queue, &cur->node,
					&timer->node);
				return;
			}
		}

		list_append(&timerq->queue, &timer->node);
	}
}

static void timerq_reconf(timerq_t *timerq, nanosec_t curtime) {
	timer_t *first;

	if(timerq->bsp || list_length(&timerq->ontick)) {
		return;
	}

	first = list_first(&timerq->queue);
	if(first == NULL) {
		evtimer_stop(timerq->dev);
		timer_debug("[timer] stopping\n");
	} else {
		timer_debug("[timer] config %lld\n", first->time - curtime);
		evtimer_config(timerq->dev, EV_ONESHOT, first->time - curtime);
	}
}

void timer_start(timer_t *timer, nanosec_t time, int flags) {
	timerq_t *timerq = timerq_get();
	nanosec_t curtime;

	curtime = nanouptime();
	timer->time =  time + curtime;
	timer->period = time;
	timer->flags = flags;
	timer_debug("[timer] adding at %lld\n", time);

	sync_scope_acquire(&timerq->lock);
	timer->tq = timerq;
	timerq_insert(timerq, timer);
	timerq_reconf(timerq, curtime);
}

void timer_ontick(timer_t *timer) {
	timer_start(timer, TICK_PERIOD, TIMER_PERIODIC | TIMER_ONTICK);
}

nanosec_t timer_stop(timer_t *timer) {
	timerq_t *timerq = timer->tq;
	nanosec_t curtime, rem;
	bool reconf = false;

	curtime = nanouptime();
	if(curtime >= timer->time) {
		rem = 0;
	} else {
		rem = timer->time - curtime;
	}

	sync_scope_acquire(&timerq->lock);
	if(timer_ontick_p(timer)) {
		reconf = list_remove(&timerq->ontick, &timer->node);
	} else if(!(timer->flags & TIMER_DONE)) {
		reconf = list_first(&timerq->queue) == timer;
		list_remove(&timerq->queue, &timer->node);
		timer->flags |= TIMER_DONE;
	}

	/*
	 * We would actually need an IPI to reconfigure the timer
	 * on another processor, so we simply do not reconfigure
	 * and live with the possibility of one sadditional
	 * spurious interrupt.
	 */
	if(reconf && timerq == timerq_get()) {
		timerq_reconf(timerq, curtime);
	}

	return rem;
}

static void timer_intr(void *arg) {
	timerq_t *timerq = arg;
	bool reconf = false;
	nanosec_t curtime;
	timer_t *timer;

	assert(timerq == timerq_get());	
	curtime = nanouptime();
	timer_debug("[timer] tick: %lld\n", curtime);

	sync_scope_acquire(&timerq->lock);
	foreach(timer, &timerq->ontick) {
		timer->func(timer->arg);
	}

	while((timer = list_first(&timerq->queue)) && timer->time <= curtime) {
		reconf = true;

		timer_debug("[timer]    calling %lld\n", timer->time);
		timer->func(timer->arg);

		list_remove(&timerq->queue, &timer->node);
		if(timer->flags & TIMER_PERIODIC) {
			/*
			 * Reinsert the timer into the queue if the timer is
			 * a periodic one.
			 */
			timer->time = curtime + timer->period;
			timerq_insert(timerq, timer);
		} else {
			timer->flags |= TIMER_DONE;
		}
	}

	/*
	 * If at a timer was removed, the timer needs to be
	 * reconfigured.
	 */
	if(reconf) {
		timerq_reconf(timerq, curtime);
	}
}

/**
 * @brief Timer interrupt callback of the boot processor.
 *
 * The timer interrupt of the boot processor is used for special
 * bookkeeping (currently timekeep only) and thus is never disabled.
 */
static void tick(void *arg) {
	/* TODO time = */ timekeep_tick();
	timer_intr(arg);
}

void __init init_timer(void) {
	timerq_t *timerq = timerq_get();

	sync_init(&timerq->lock, SYNC_SPINLOCK);
	list_init(&timerq->queue);
	list_init(&timerq->ontick);

	/*
	 * We need one periodic tick on one processor (actually it's the BSP)
	 * for housekeeping.
	 */
	timerq->dev = evtimer_get(EV_F_ONESHOT | EV_F_PERIODIC | EV_F_CPULOCAL,
		bsp_p() ? tick : timer_intr, timerq);
	if(timerq->dev == NULL) {
		kpanic("[timer] no event timer for CPU%d\n", cur_cpu()->id);
	}

	if(bsp_p()) {
		timerq->bsp = true;

		/*
		 * Configure the timer to periodically tick.
		 */
		evtimer_config(timerq->dev, EV_PERIODIC, TICK_PERIOD);
	}
}

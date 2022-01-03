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
#include <kern/sync.h>
#include <kern/time.h>
#include <kern/cpu.h>
#include <kern/mp.h>
#include <arch/barrier.h>
#include <device/evtimer.h>

static DEFINE_LIST(evtimer_list);
static sync_t evtimer_lock = SYNC_INIT(MUTEX);

void evtimer_intr(evtimer_t *timer) {
	if(timer->callback) {
		timer->callback(timer->arg);
	}
}

void evtimer_config(evtimer_t *timer, evtimer_mode_t mode, nanosec_t time) {
	uint64_t div;

	time = clamp(time, timer->min_period, timer->max_period);
	div = (time * timer->freq) / SEC_NANOSECS;
	timer->config(timer, mode, div);
}

void evtimer_stop(evtimer_t *timer) {
	timer->stop(timer);
}

void evtimer_register(evtimer_t *timer) {
	assert(timer->config);
	assert(timer->stop);

	kprintf("[evtimer] registered timer: %s\n", timer->name);

	list_node_init(timer, &timer->node);
	timer->callback = NULL;
	sync_scope_acquire(&evtimer_lock);
	list_append(&evtimer_list, &timer->node);
}

int evtimer_unregister(evtimer_t *timer) {
	synchronized(&evtimer_lock) {
		if(timer->callback) {
			return -EBUSY;
		} else {
			list_remove(&evtimer_list, &timer->node);
		}
	}

	list_node_destroy(&timer->node);
	return 0;
}

evtimer_t *evtimer_get(int flags, ev_callback_t cb, void *arg) {
	evtimer_t *timer;
	int id, rflags;

	id = cur_cpu()->id;
	rflags = flags & ~EV_F_CPULOCAL;

	sync_scope_acquire(&evtimer_lock);
	foreach(timer, &evtimer_list) {
		if((timer->flags & flags) == rflags &&
			(!(flags & EV_F_CPULOCAL) || !mp_capable() ||
			id == timer->cpu))
		{

			/*
			 * TODO
			 * What if a concurrent call to evtimer_intr happens
			 * on an arch with weak memory ordering or SMP?
			 * (just a background info: atpit interrupt always
			 * calls evtimer_intr, even after evtimer_stop).
			 */
			timer->arg = arg;
			timer->callback = cb;

			kprintf("[evtimer] choosing %s for CPU%d\n",
				timer->name, id);
			return timer;
		}
	}

	return NULL;
}

void evtimer_put(evtimer_t *timer) {
	sync_scope_acquire(&evtimer_lock);
	timer->callback = NULL;
}

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
#include <kern/wait.h>
#include <kern/proc.h>
#include <kern/critical.h>
#include <kern/timer.h>
#include <kern/sched.h>
#include <sys/limits.h>

/**
 * The following two flags are stored in thread_flags. Furtheremore
 * thread_flags contains the pointer to the waiting thread.
 */

/**
 * This waiter is woken up.
 */
#define WAIT_DONE (1 << 0)

/**
 * The thread is currently sleeping and should be woken up.
 */
#define WAIT_SLEEP (1 << 1)
#define WAIT_FLAGS (WAIT_DONE | WAIT_SLEEP)
#define WAIT_THREAD (~WAIT_FLAGS)

static inline thread_t *waiter_thread(waiter_t *wait) {
	return (thread_t *)(wait->thread_flags & WAIT_THREAD);
}

static unsigned waiter_flags(waiter_t *wait) {
	return wait->thread_flags & WAIT_FLAGS;
}

void wait_prep(waitqueue_t *wq, waiter_t *wait) {
	thread_t *thread = cur_thread();

	wait->thread_flags = (uintptr_t)thread;
	assert(waiter_flags(wait) == 0);

	synchronized(&wq->lock) {
		list_append(&wq->waiters, &wait->node);
	}
}

void wait_abort(waitqueue_t *wq, waiter_t *wait) {
	sync_scope_acquire(&wq->lock);

	if(!F_ISSET(wait->thread_flags, WAIT_DONE)) {
		list_remove(&wq->waiters, &wait->node);
	}
}

static __unused void wait_timeout(void *arg) {
	sched_wakeup(waiter_thread(arg), SCHED_NORMAL);
}

int wait_sleep_timeout(waitqueue_t *wq, waiter_t *wait, int flags,
	struct timespec *timeout)
{
	const bool may_intr = !!(flags & WAIT_INTERRUPTABLE);
	nanosec_t nanosec = 0;
	timer_t timer;
	int retv = 0;

	assert_not_critsect("[wait] trying to sleep during critical section");

	/*
	 * Well mh, ... TODO.
	 */
	if(timeout) {
		assert(flags & WAIT_INTERRUPTABLE);
	}

	/*
	 * Don't even try to sleep, if the thread was interrupted.
	 */
	if(F_ISSET(flags, WAIT_INTERRUPTABLE) &&
		(retv = sched_interrupted()) < 0)
	{
		goto intr;
	}

	if(timeout) {
		timer_init(&timer, wait_timeout, wait);

		/*
		 * The timer-code usually works with nanoseconds rather than
		 * struct timespec.
		 */
		nanosec = ts_to_nsec(timeout);
	}

	/*
	 * Suspend the thread.
	 */
	critical {
		synchronized(&wq->lock) {
			/*
			 * The wakeup() might have happend between the prep()
			 * and the sleep().
			 */
			if(F_ISSET(wait->thread_flags, WAIT_DONE)) {
				goto out;
			}

			F_SET(wait->thread_flags, WAIT_SLEEP);
			sched_set_inactive(may_intr);
		}

		/*
		 * Make sure someone calls wait_timeout() after the
		 * right time. Since sched_set_inactive() was called above,
		 * the thread stops execution after the next preemption. The
		 * critical section prevents this from happening until the
		 * timeout event was registered.
		 */
		if(timeout) {
			timer_start(&timer, nanosec, TIMER_ONESHOT);
		}
	}

	/*
	 * Sleep until we're woken up again by wakeup().
	 *
	 * TODO do sth like: atomic_schedule_if_sleep_flag_still_set()
	 */
	schedule();

	if(timeout) {
		nanosec_t remaining;

		/*
		 * Try to abort the invokation of the event.
		 */
		remaining = timer_stop(&timer);
		if(remaining == 0) {
			/*
			 * The wait timed out.
			 */
			timeout->tv_sec = 0;
			timeout->tv_nsec = 0;
			retv = -ETIMEDOUT;
			goto timeout;
		}

		/*
		 * Store the remaining time in the timeout timespec.
		 */
		nsec_to_ts(remaining, timeout);
	}

	if(F_ISSET(flags, WAIT_INTERRUPTABLE) &&
		(retv = sched_interrupted()) < 0)
	{
		goto intr;
	}

	assert(F_ISSET(wait->thread_flags, WAIT_DONE));
	goto out;

intr:
timeout:
	/*
	 * If the thread was woken up because of a software interrupt or due
	 * to the timeout, it might still be on the waitqueue.
	 */
	wait_abort(wq, wait);
out:
	if(timeout) {
		timer_destroy(&timer);
	}

	return retv;
}

void wakeup_waiter(waitqueue_t *wq, waiter_t *wait, sched_prio_t prio) {
	sync_assert(&wq->lock);
	assert(!(wait->thread_flags & WAIT_DONE));

	list_remove(&wq->waiters, &wait->node);
	F_SET(wait->thread_flags, WAIT_DONE);

	/*
	 * Don't call sched_wakeup() if the waiting thread did not
	 * call wait_sleep() yet. Otherwise this might spuriously
	 * wakeup the thread (threads are allowed to sleep between
	 * wait_prep() and wait_sleep())
	 */
	if(F_ISSET(wait->thread_flags, WAIT_SLEEP)) {
		sched_wakeup(waiter_thread(wait), prio);
	}
}

void wakeup_num(waitqueue_t *wq, sched_prio_t prio, size_t num) {
	waiter_t *wait;
	size_t i = 0;

	waitq_foreach(wait, wq) {
		if(i++ == num) {
			return;
		} else {
			wakeup_waiter(wq, wait, prio);
		}
	}
}

void wakeup(waitqueue_t *wq, sched_prio_t prio) {
	wakeup_num(wq, prio, SIZE_MAX);
}

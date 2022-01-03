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
#include <kern/time.h>
#include <kern/init.h>
#include <kern/atomic.h>
#include <kern/user.h>
#include <kern/wait.h>

#define leapyear(y) \
	(((y) % 4 == 0) && (((y) % 100 != 0) || ((y) % 400 == 0)))
#define year_days(y) (leapyear(y) ? 366 : 365)

#define SLEEPQ_MASK	(NSLEEPQ - 1)
#define NSLEEPQ		32

static waitqueue_t sleep_wq[NSLEEPQ];
static uint8_t sleep_idx = 0;

/*
 * Precalculated number of days since 1970 until 2017.
 */
static int cur_year = 2017;
static int cur_year_days = 17167;

static int month_days(int year, int month) {
	static const int days[12] = {
		31, -1, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};

	assert(month >= 1 && month <= 12);
	if(month == 2) {
		return leapyear(year) ? 29 : 28;
	} else {
		return days[month - 1];
	}
}

static int year_month_days(int year, int month) {
	int days = 0;

	switch(month) {
	case 11: days += 30;
	/* FALLTHROUGH */
	case 10: days += 31;
	/* FALLTHROUGH */
	case 9:	days += 30;
	/* FALLTHROUGH */
	case 8: days += 31;
	/* FALLTHROUGH */
	case 7: days += 31;
	/* FALLTHROUGH */
	case 6: days += 30;
	/* FALLTHROUGH */
	case 5: days += 31;
	/* FALLTHROUGH */
	case 4: days += 30;
	/* FALLTHROUGH */
	case 3: days += 31;
	/* FALLTHROUGH */
	case 2:
		days += 28;
		if(leapyear(year)) {
			days++;
		}
	/* FALLTHROUGH */
	case 1: days += 31;
	/* FALLTHROUGH */
	default:
		break;
	}

	return days;
}

int datetime_to_ts(datetime_t *time, struct timespec *ts) {
	int year, days, i, hours, mins;

	days = 0;
	year = time->year;

	if(time->sec >= 60 || time->min >= 60 || time->hour >= 24 ||
		time->mon < 1 || time->mon > 12 || time->day < 1 ||
		time->day > month_days(year, time->mon))
	{
		return -EINVAL;
	}

	if(year >= cur_year) {
		days = cur_year_days;
		i = cur_year;
	} else { 
		i = YEAR_BASE;
	}

	while(i < year) {
		days += year_days(i);
		i++;
	}

	days += year_month_days(year, time->mon - 1);
	days += time->day - 1;

	hours = days * 24 + time->hour;
	mins = hours * 60 + time->min;

	ts->tv_nsec = time->nsec;
	ts->tv_sec = mins * 60 + time->sec;

	return 0;
}

static waitqueue_t *sleep_wq_get(void) {
	return &sleep_wq[atomic_inc_relaxed(&sleep_idx) & SLEEPQ_MASK];
}

int sleep_timespec(struct timespec *ts) {
	waitqueue_t *wq = sleep_wq_get();
	waiter_t wait;
	int err;

	/*
	 * Using the timeout function from the wait() interface for
	 * implementing the sleep. None of the waiters in any sleep_wq
	 * will be woken up and thus they all just wait for timeout
	 * (or signal, ...).
	 */
	wait_init_prep(wq, &wait);
	err = wait_sleep_timeout(wq, &wait, WAIT_INTERRUPTABLE, ts);
	waiter_destroy(&wait);

	/*
	 * A timeout of wait() is actually a success here.
	 */
	if(err == -ETIMEDOUT) {
		err = 0;
	} else if(err == -ERESTART) {
		err = -EINTR;
	}

	return err;
}

void ts_add(struct timespec *a, struct timespec *b, struct timespec *res) {
	assert(a->tv_nsec < SEC_NANOSECS);
	assert(b->tv_nsec < SEC_NANOSECS);
	assert(a->tv_nsec >= 0);
	assert(b->tv_nsec >= 0);

	res->tv_sec = a->tv_sec + b->tv_sec;
	res->tv_nsec = a->tv_nsec + b->tv_nsec;
	if(res->tv_nsec >= SEC_NANOSECS) {
		res->tv_nsec -= SEC_NANOSECS;
		res->tv_sec++;
	}
}

void ts_inc(struct timespec *ts, nanosec_t ns) {
	struct timespec tmp;
	nsec_to_ts(ns, &tmp);
	ts_add(ts, &tmp, ts);
}

int copyin_ts(struct timespec *ts, const struct timespec *uts) {
	int err;

	err = copyin(ts, uts, sizeof(*ts));
	if(err) {
		return err;
	}

	if(ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= SEC_NANOSECS) {
		err = -EINVAL;
	}

	return err;
}

/*
 * TODO REMOVE
 */
void realtime(struct timespec *time) {
	struct timespec up, boot;
	gettsboottime(&boot);
	gettsuptime(&up);
	ts_add(&up, &boot, time);
}

int sys_clock_gettime(clockid_t id, struct timespec *out) {
	struct timespec ts;

	switch(id) {
	case CLOCK_REALTIME:
		realtime(&ts);
		break;
	case CLOCK_MONOTONIC:
		/* FALLTHROUGH */
	default:
		kprintf("[time] warning: clock_gettime: %d\n", id);
		return -ENOTSUP;
	}

	return copyout(out, &ts, sizeof(ts));
}

int sys_clock_getres(clockid_t clk, struct timespec *ts) {
	(void) clk;

	if(ts == NULL) {
		return 0;
	}

	return -ENOTSUP;
}

int sys_nanosleep(const struct timespec *ureq, struct timespec *urem) {
	struct timespec timeout;
	int err;

	err = copyin_ts(&timeout, ureq);
	if(err) {
		return err;
	}

	err = sleep_timespec(&timeout);
	if(err == -EINTR) {
		/*
		 * Copy the remaining time back to userspace.
		 */
		err = copyout(urem, &timeout, sizeof(timeout));
		if(err == 0) {
			err = -EINTR;
		}
	}

	return err;
}

static __init int time_init(void) {
	for(size_t i = 0; i < NSLEEPQ; i++) {
		waitqueue_init(&sleep_wq[i]);
	}

	return INIT_OK;
}

early_initcall(time_init);
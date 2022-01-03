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
#include <kern/atomic.h>
#include <kern/time.h>
#include <kern/critical.h>
#include <device/timecounter.h>
#include <device/rtc.h>

/**
 * @brief Indicator that a timehands is currently updated.
 */
#define TC_GEN_UPDATE	0

/**
 * @brief The initial value for a timehands generation.
 */
#define TC_GEN_INIT	1

typedef struct timehands {
	struct timehands *next;
	unsigned gen;
	uint64_t last_count;
	nanosec_t nanotime;
	struct timespec tstime;
} timehands_t;

static timehands_t th1;
static timehands_t th0 = {
	.next = &th1,
	.gen = TC_GEN_INIT,
};
static timehands_t th1 = {
	.next = &th0,
	.gen = TC_GEN_INIT,
};
static timehands_t *volatile timehands = &th0;
static timecounter_t *tc_list, *timecounter;

/**
 * @brief The real time of the boot.
 * TODO per timehands boot time, so that it
 * can be adjusted using NTP
 */
static struct timespec boottime;

static inline bool timekeep_retry(unsigned gen, timehands_t *th) {
	return gen == TC_GEN_UPDATE || atomic_load_acquire(&th->gen) != gen;
}

static inline nanosec_t timekeep_delta(timehands_t *th) {
	uint64_t delta;

	assert(timecounter);
	assert(timecounter->read);

	delta = (tc_read(timecounter) - th->last_count) & timecounter->mask;
	return ((nanosec_t)delta * SEC_NANOSECS) / timecounter->freq;
}

static inline void timekeep_ts_add(struct timespec *ts, nanosec_t nsec) {
	ts->tv_nsec += nsec;

	if(ts->tv_nsec >= SEC_NANOSECS) {
		ts->tv_sec++;
		ts->tv_nsec -= SEC_NANOSECS;
		assert(ts->tv_nsec < SEC_NANOSECS);
	}
}

nanosec_t timekeep_tick(void) {
	uint64_t delta, count;
	nanosec_t nsdelta;
	timehands_t *th;
	unsigned gen;

	/*
	 * Initialize the fields in the next timehands. The timings
	 * of the last time window are still in the last timehands.
	 * This allows us to update the system time while others
	 * may concurrently still read the system time.
	 */
	th = timehands->next;
	gen = atomic_xchg_relaxed(&th->gen, TC_GEN_UPDATE);

	/*
	 * Read the new time from the timecounter.
	 */
	count = tc_read(timecounter);
	delta = (count - th->last_count) & timecounter->mask;
	th->last_count = count;
	nsdelta = ((nanosec_t)delta * SEC_NANOSECS) / timecounter->freq;

	/*
	 * Update the time.
	 */
	th->nanotime += nsdelta;
	timekeep_ts_add(&th->tstime, nsdelta);
	if(++gen == TC_GEN_UPDATE) {
		gen = TC_GEN_INIT;
	}

	atomic_store_release(&th->gen, gen);
	timehands = th;

	return th->nanotime;
}

nanosec_t nanouptime(void) {
	nanosec_t retv;
	timehands_t *th;
	unsigned gen;

	do {
		th = timehands;
		gen = atomic_load_acquire(&th->gen);
		retv = th->nanotime + timekeep_delta(th);
	} while(timekeep_retry(gen, th));

	return retv;
}

nanosec_t getnanouptime(void) {
	nanosec_t retv;
	timehands_t *th;
	unsigned gen;

	do {
		th = timehands;
		gen = atomic_load_acquire(&th->gen);
		retv = th->nanotime;
	} while(timekeep_retry(gen, th));

	return retv;
}

void tsuptime(struct timespec *time) {
	timehands_t *th;
	unsigned gen;

	do {
		th = timehands;
		gen = atomic_load_acquire(&th->gen);
		*time = th->tstime;
		timekeep_ts_add(time, timekeep_delta(th));
	} while(timekeep_retry(gen, th));
}

void gettsuptime(struct timespec *time) {
	timehands_t *th;
	unsigned gen;

	do {
		th = timehands;
		gen = atomic_load_acquire(&th->gen);
		*time = th->tstime;
	} while(timekeep_retry(gen, th));
}

void gettsboottime(struct timespec *time) {
	struct timespec tmp;
	gettsuptime(&tmp);
	ts_add(&boottime, &tmp, time);
}

void ndelay(nanosec_t nsec) {
	int64_t left;
	uint64_t prev;

	prev = tc_read(timecounter);
	left = (nsec * timecounter->freq + (SEC_NANOSECS - 1)) / SEC_NANOSECS;

	while(left > 0) {
		uint64_t cntr, delta;

		cntr = tc_read(timecounter);
		delta = (cntr - prev) & timecounter->mask;
		prev = cntr;
		left -= delta;
	}
}

void tc_register(timecounter_t *tc) {
	tc->next = tc_list;
	tc_list = tc;

	/*
	 * TODO choose the time counter based on quality
	 */
	if(timecounter == NULL || tc->quality > timecounter->quality) {
		timecounter = tc;
	}
}

int tc_unregister(timecounter_t *tc) {
	(void) tc;
	return -EBUSY;
}

void __init init_timekeep(void) {
	/*
	 * We need somebody for keeping track of time.
	 * TODO could implement a dummy timecounter, which
	 * uses the periodic tick() on BSP for measuring the
	 * time if there is no other timer.
	 */
	assert(timecounter);
	kprintf("[time] using counter: %s\n", timecounter->name);

	/*
	 * Get the real time if the boot using a real time clock.
	 * If none is present the boot time is just zero.
	 */
	rtc_time(&boottime);
}

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
#include <device/rtc.h>

static DEFINE_LIST(rtc_list);
static sync_t rtc_lock = SYNC_INIT(MUTEX);

void rtc_register(rtcdev_t *rtc) {
	list_node_init(rtc, &rtc->node);

	sync_scope_acquire(&rtc_lock);
	list_append(&rtc_list, &rtc->node);
}

void rtc_unregister(rtcdev_t *rtc) {
	synchronized(&rtc_lock) {
		list_remove(&rtc_list, &rtc->node);
	}

	list_node_destroy(&rtc->node);
}

void rtc_time(struct timespec *ts) {
	rtcdev_t *dev;
	int err;

	sync_scope_acquire(&rtc_lock);
	dev = list_first(&rtc_list);
	if(dev) {
		err = dev->gettime(dev, ts);
		if(err == RTC_OK) {
			return;
		}

		kprintf("[rtc] could not read the time: %d\n", err);
	} else {
		kprintf("[rtc] warning: no real time clock present\n");
	}

	ts->tv_sec = 0;
	ts->tv_nsec = 0;
}

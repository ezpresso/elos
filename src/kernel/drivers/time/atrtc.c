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
#include <kern/sync.h>
#include <kern/time.h>
#include <device/device.h>
#include <device/rtc.h>
#include <device/nvram.h>
#include <drivers/bus/isa.h>
#include <lib/bin.h>

#define ATRTC(ptr) container_of(ptr, atrtc_t, rtc)
#define NVRAM_ATRTC(ptr) container_of(ptr, atrtc_t, nvram)
typedef struct atrtc {
	nvram_t nvram;
	bus_res_t io;
	bus_res_t intr;
	uint8_t cur_sel;

	rtcdev_t rtc;
	sync_t lock;
} atrtc_t;

/* IO ports */
#define ADDR		0x0 /* Select */
#define 	NMI_DISABLE	(1 << 7)
#define 	ADDR_MASK	~NMI_DISABLE
#define DATA		0x1

/* Registers */
#define RTC_SEC 	0x00
#define RTC_MIN		0x02
#define RTC_HRS		0x04
#define RTC_WDAY	0x06 /* Day of week */
#define RTC_MDAY 	0x07 /* Day of month */
#define RTC_MNTH	0x08
#define RTC_YEAR	0x09
#define	RTC_CENTURY	0x32

#define STATUSA		0x0a
#define 	SA_UPDATE	(1 << 7)
#define STATUSB		0x0b
#define INTR 		0x0c
#define STATUSD		0x0d

static nvram_driver_t atrtc_nvram;

static inline void atrtc_select(atrtc_t *rtc, uint8_t off) {
	if(rtc->cur_sel != off) {
		bus_res_writeb(&rtc->io, ADDR, off);
		rtc->cur_sel = off;
	}
}

static uint64_t atrtc_readb(atrtc_t *rtc, uint8_t off) {
	atrtc_select(rtc, off);
	return bus_res_readb(&rtc->io, DATA);
}

static void atrtc_writeb(atrtc_t *rtc, uint8_t off, uint8_t val) {
	atrtc_select(rtc, off);
	bus_res_writeb(&rtc->io, DATA, val);
}

static int atrtc_nvram_rw(nvram_t *nvram, bool wr, uint64_t off, uint8_t size,
	uint64_t *value)
{
	atrtc_t *rtc = NVRAM_ATRTC(nvram);

	if(size != 1 || off > UINT8_MAX) {
		return -ENOTSUP;
	}

	sync_scope_acquire(&rtc->lock);
	if(wr) {
		atrtc_writeb(rtc, off, *value);
	} else {
		*value = atrtc_readb(rtc, off);
	}

	return 0;
}

static int atrtc_gettime(rtcdev_t *dev, struct timespec *ts) {
	atrtc_t *rtc = ATRTC(dev);
	datetime_t time;

	/*
	 * TODO check for battery failure
	 */
	synchronized(&rtc->lock) {
		/*
		 * TODO timeout?
		 */
		while(atrtc_readb(rtc, STATUSA) & SA_UPDATE) {
			continue;
		}

		time.sec = bcd2bin(atrtc_readb(rtc, RTC_SEC));
		time.min = bcd2bin(atrtc_readb(rtc, RTC_MIN));
		time.hour = bcd2bin(atrtc_readb(rtc, RTC_HRS));
		time.day = bcd2bin(atrtc_readb(rtc, RTC_MDAY));
		time.mon = bcd2bin(atrtc_readb(rtc, RTC_MNTH));
		time.year = bcd2bin(atrtc_readb(rtc, RTC_YEAR)) +
			bcd2bin(atrtc_readb(rtc, RTC_CENTURY)) * 100;
		time.nsec = 0;
	}

	return datetime_to_ts(&time, ts) ? RTC_ERROR : RTC_OK;
}

static int atrtc_probe(__unused device_t *device) {
	return DEVICE_PROBE_OK;
}

static int atrtc_attach(device_t *device) {
	atrtc_t *rtc = device_priv(device);
	int err;

	err = bus_alloc_res_name(device, BUS_RES_IO, 0, "io", RF_MAP,
		&rtc->io);
	if(err) {
		return err;
	}

	err = bus_alloc_res_name(device, BUS_RES_INTR, 0, "intr", RF_NONE,
		&rtc->intr);
	if(err) {
		goto err_intr;
	}

	rtc->cur_sel = bus_res_readb(&rtc->io, ADDR) & ADDR_MASK;
	err = nvram_register(&rtc->nvram, &atrtc_nvram, "cmos");
	if(err) {
		goto err_nvram;
	}

	sync_init(&rtc->lock, SYNC_SPINLOCK);
	rtc->rtc.resolution = SEC2NANO(1);
	rtc->rtc.gettime = atrtc_gettime;

	rtc_register(&rtc->rtc);
	device_set_desc(device, "AT CMOS and real-time clock");
	return 0;

err_nvram:
	bus_free_res(device, &rtc->intr);
err_intr:
	bus_free_res(device, &rtc->io);
	return err;
}

static int atrtc_detach(device_t *device) {
	atrtc_t *rtc = device_priv(device);

	rtc_unregister(&rtc->rtc);
	nvram_unregister(&rtc->nvram);
	bus_free_res(device, &rtc->intr);
	bus_free_res(device, &rtc->io);
	sync_destroy(&rtc->lock);

	return 0;
}

static nvram_driver_t atrtc_nvram =  {
	.nvram_rw = atrtc_nvram_rw,
};

static DEVTAB(atrtc_devtab, "atrtc", "isa") {
	{ .id = ISA_ID(ISA_VEN_PNP, 0x0B00) /* PNP0B00 */ }, 
	{ DEVTAB_END }
};

static DRIVER(atrtc_driver) {
	.name = "atrtc",
	.flags = DEV_DEP_INTR,
	.type = DEVICE_CLOCK,
	.private = sizeof(atrtc_t),
	.devtab = &atrtc_devtab,
	.probe = atrtc_probe,
	.attach = atrtc_attach,
	.detach = atrtc_detach,
};

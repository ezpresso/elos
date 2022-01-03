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
#include <device/device.h>
#include <device/timecounter.h>
#include <drivers/bus/isa.h>

#define HPET_CAP_ID	0x000 /* 8b, rd, General Capabilities and ID Register */
#define 	HPET_REVID(x)	(uint8_t)GETBITS(x, 0, 8)
#define 	HPET_NTIM(x)	(uint8_t)GETBITS(x, 8, 5)
#define		HPET_CNTSZ(x)	(uint8_t)GETBITS(x, 13, 1)
#define		HPET_LEG(x)	(uint8_t)GETBITS(x, 15, 1)
#define		HPET_VEN(x)	(uint16_t)GETBITS(x, 16, 16)
#define		HPET_PERIOD(x)	(uint32_t)GETBITS(x, 32, 32)
#define			HPET_PERIOD_MAX 0x05F5E100
#define HPET_CONF	0x010 /* 8b, rdwr, General Configuration Register */
#define		HPET_ENABLE 	(1 << 0)
#define		HPET_LEG_RT_CNF	(1 << 1)
#define HPET_INTSTA	0x020 /* 8b, rdwc, General Interrupt Status Register */
#define HPET_CNTR	0x0f0 /* 8b, rdwr, Main Counter Value Register */

typedef struct hpet {
	timecounter_t tc;
	bus_res_t regs;
	/* bus_res_t intr; */
} hpet_t;

#if 0
static int hpet_intr(__unused device_t *device, __unused int intr) {
	device_printf(device, "INTR: %d\n", intr);
	return BUS_INTR_OK;
}
#endif

static int hpet_probe(__unused device_t *device) {
	return DEVICE_PROBE_OK;
}

static uint64_t hpet_tc_read(timecounter_t *tc) {
	hpet_t *hpet = tc_priv(tc);
	return bus_res_readq(&hpet->regs, HPET_CNTR);
}

static int hpet_attach(device_t *device) {
	hpet_t *hpet = device_priv(device);
	uint32_t period;
	uint64_t cap;
	int err;

	err = bus_alloc_res(device, BUS_RES_MEM, 0, RF_MAP, &hpet->regs);
	if(err) {
		return err;
	}

	cap = bus_res_readq(&hpet->regs, HPET_CAP_ID);
	period = HPET_PERIOD(cap);
	if(period > HPET_PERIOD_MAX) {
		device_printf(device, "period too large: %d\n", period);
		return -ENXIO;
	}

	hpet->tc.name = "hpet";
	hpet->tc.freq = (uint32_t)(1000000000000000ULL / period);
	hpet->tc.mask = UINT64_MAX;
	hpet->tc.quality = 100;
	hpet->tc.priv = hpet;
	hpet->tc.read = hpet_tc_read;

	device_printf(device, "vendor: 0x%x\n"
			"\trevision: %d\n"
			"\ttimer num: %d\n"
			"\tcounter size: %s\n"
			"\tlegacy route: %s\n"
			"\tfrequency: %lldHz\n",
			HPET_VEN(cap), HPET_REVID(cap), HPET_NTIM(cap),
			HPET_CNTSZ(cap) ? "64bit" : "32bit",
			HPET_LEG(cap) ? "true" : "false", hpet->tc.freq);
	device_set_desc(device, "High Precision Event Timer");

	bus_res_maskl(&hpet->regs, HPET_CONF, 0, HPET_ENABLE);
	tc_register(&hpet->tc);

 	return 0;
}

static int hpet_detach(device_t *device) {
	hpet_t *hpet = device_priv(device);

	bus_free_res(device, &hpet->regs);

	return 0;
}

static DEVTAB(hpet_devtab, "hpet", "isa") {
	{ .id = ISA_ID(ISA_VEN_PNP, 0x0103) },
	{ DEVTAB_END }
};

static DRIVER(hpet_driver) {
	.name = "hpet",
	.flags = DEV_DEP_INTR /*| DEV_DEP_DYN */,
	.type = DEVICE_TIMER,
	.private = sizeof(hpet_t),
	.devtab = &hpet_devtab,
	.probe = hpet_probe,
	.attach = hpet_attach,
	.detach = hpet_detach,
};

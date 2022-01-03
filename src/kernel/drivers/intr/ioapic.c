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
#include <device/device.h>
#include <device/intr.h>
#include <vm/malloc.h>

#define IOREGSEL	0x00
#define IOWIN		0x10
#define EOIR		0x20

#define ID 		0x00
#define VER 		0x01
#define 	VERSION_MASK	0xFF
#define 	REDIR_NUM_SHIFT	16
#define 	REDIR_NUM_MASK	0xFF
#define ARB 		0x02
#define IOREDTBL0	0x10
#define IOREDTBL(n)	(IOREDTBL0 + 2 * (n))
#define		INTVEC(x) (x)
#define 	DELMOD(x) (DELMOD_ ## x << 8)
#define 	DELMOD_MASK	(7 << 8)
#define 		DELMOD_FIXED 	0
#define			DELMOD_LOPRIO	1
#define			DELMOD_SMI	2
#define			DELMOD_NMI	4
#define			DELMOD_INIT	5
#define			DELMOD_EXTINT	7
#define 	DESTMOD_PHYS	(0 << 11)
#define 	DESTMOD_LOGICAL	(1 << 11)
#define		DELIVS		(1 << 12) /* Delivery Status */
#define		INTPOL_HI	(0 << 13)
#define		INTPOL_LO	(1 << 13)
#define		REMOTE_IRR	(1 << 14)
#define		TRIG_EDGE	(0 << 15)
#define		TRIG_LEVEL	(1 << 15)
#define		MASKED		(1 << 16)
#define 	DEST_SHIFT	56
#define INTVEC_MIN 	0x10
#define INTVEC_MAX	0xFE

#define IOAPIC(intcntlr) container_of(intcntlr, ioapic_t, cntlr)
typedef struct ioapic {
	device_t *self;

	intr_cntlr_t cntlr;
	intr_src_t *intrs;

	bus_res_t mmio;
	bus_res_t cpu_intr;
	size_t intr_off;

	uint8_t nintr;
} ioapic_t;

static __unused uint32_t ioapic_read(ioapic_t *pic, uint8_t addr) {
	bus_res_writel(&pic->mmio, IOREGSEL, addr);
	return bus_res_readl(&pic->mmio, IOWIN);
}

static __unused void ioapic_write(ioapic_t *pic, uint8_t addr, uint32_t val) {
	bus_res_writel(&pic->mmio, IOREGSEL, addr);
	bus_res_writel(&pic->mmio, IOWIN, val);
}

static void ioapic_set_ioredtbl(ioapic_t *pic, size_t i, uint64_t value) {
	ioapic_write(pic, IOREDTBL(i) + 0, value & 0xFFFFFFFF);
	ioapic_write(pic, IOREDTBL(i) + 1, value >> 32);
}

static bool ioapic_config(intr_cntlr_t *cntlr, intnum_t num, int flags) {
	ioapic_t *pic = IOAPIC(cntlr);
	uint64_t conf;

	device_printf(pic->self, "config %d: %s, active %s, %s => %d\n",
		num, BUS_IS_TRIG_LEVEL(flags) ? "level" : "edge",
		BUS_IS_POL_HIGH(flags) ? "high" : "low",
		(flags & BUS_INTR_MASKED) ? "masked" : "active",
		num + pic->intr_off);

	conf = DESTMOD_PHYS | (0ULL << DEST_SHIFT) | DELMOD(FIXED) |
		INTVEC(num + pic->intr_off);

	conf |= BUS_IS_TRIG_LEVEL(flags) ? TRIG_LEVEL : TRIG_EDGE;
	conf |= BUS_IS_POL_HIGH(flags) ? INTPOL_HI : INTPOL_LO;
	conf |= (flags & BUS_INTR_MASKED) ? MASKED : 0;
	ioapic_set_ioredtbl(pic, num, conf);

	return true;
}

static int ioapic_intr(void *arg, int num) {
	ioapic_t *pic = arg;
	return intr_handle(&pic->cntlr, num - pic->intr_off);
}

static int ioapic_alloc_res(device_t *device, bus_res_req_t *req) {
	ioapic_t *pic = device_priv(device);
	int err;

	err = intr_alloc(&pic->cntlr, req);
	if(!err) {
		device_add_ref(device, bus_res_get_device(req->res));
	}

	return err;
}

static void ioapic_free_res(device_t *device, bus_res_t *res) {
	ioapic_t *pic = device_priv(device);
	intr_free(&pic->cntlr, res);
	device_remove_ref(device, bus_res_get_device(res));
}

static int ioapic_setup_res(device_t *device, bus_res_t *res) {
	ioapic_t *pic = device_priv(device);
	intr_add_handler(&pic->cntlr, res);
	return 0;
}

static void ioapic_teardown_res(device_t *device, bus_res_t *res) {
	ioapic_t *pic = device_priv(device);
	intr_remove_handler(&pic->cntlr, res);
}

static int ioapic_probe(device_t *device) {
	(void) device;
	return DEVICE_PROBE_OK;
}

static int ioapic_attach(device_t *device) {
	ioapic_t *pic = device_priv(device);
	uint32_t ver;
	int err;

	err = bus_alloc_res_name(device, BUS_RES_MEM, 0, "mmio", RF_MAP,
		&pic->mmio);
	if(err) {
		return err;
	}

	ver = ioapic_read(pic, VER);
	pic->nintr = ((ver >> REDIR_NUM_SHIFT) & REDIR_NUM_MASK) + 1;
	pic->self = device;

	/*
	 * Try allocating some interrupts from the cpus.
	 */
	err = bus_alloc_res_at(device, BUS_RES_INTR,
		0, /* bus_id */
		INTVEC_MIN, /* start */
		INTVEC_MAX, /* end */
		pic->nintr, /* size*/
		1, /* align */
		RF_NONE, /* flags */
		&pic->cpu_intr);
	if(err) {
		bus_free_res(device, &pic->mmio);
		return err;
	}

	pic->intr_off = bus_res_get_addr(&pic->cpu_intr);
	err = bus_setup_intr(device, ioapic_intr, NULL, pic, &pic->cpu_intr);
	if(err) {
		bus_free_res(device, &pic->mmio);
		bus_free_res(device, &pic->cpu_intr);
		return err;
	}

	pic->intrs = kmalloc(sizeof(intr_src_t) * pic->nintr, VM_WAIT);
	pic->cntlr.config = ioapic_config;
	intr_cntlr_init(&pic->cntlr, pic->intrs, pic->nintr);
	for(size_t i = 0; i < pic->nintr; i++) {
		uint64_t conf = MASKED | DESTMOD_PHYS | DELMOD(FIXED) |
			INTPOL_HI | TRIG_EDGE | INTVEC(i + pic->intr_off);
		ioapic_set_ioredtbl(pic, i, conf);
		intr_config(&pic->cntlr, i, BUS_POL_HI | BUS_TRIG_EDGE);
	}

	device_set_desc(device, "I/O APIC");
	return 0;
}

static int ioapic_detach(device_t *device) {
	ioapic_t *pic = device_priv(device);

	bus_free_res(device, &pic->cpu_intr);
	bus_free_res(device, &pic->mmio);
	intr_cntlr_destroy(&pic->cntlr);
	kfree(pic->intrs);

	return 0;
}

static bus_driver_t ioapic_bus_driver = {
	.bus_alloc_res = ioapic_alloc_res,
	.bus_free_res = ioapic_free_res,
	.bus_setup_res = ioapic_setup_res,
	.bus_teardown_res = ioapic_teardown_res,
};

static DRIVER(ioapic_driver) {
	.name = "ioapic",
	.flags = DEV_DEP_INTR,
	.type = DEVICE_INTR_CNTLR,
	.private = sizeof(ioapic_t),
	.probe = ioapic_probe,
	.attach = ioapic_attach,
	.detach = ioapic_detach,
	.bus = &ioapic_bus_driver,
};

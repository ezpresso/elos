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
#include <drivers/bus/pci.h>

typedef struct i8254x_device {
	bus_res_t regs;
	bus_res_t intr;
} i8254x_device_t;

static int i8254x_probe(device_t *device) {
	(void) device;
	return DEVICE_PROBE_OK;
}

static int i8254x_attach(device_t *device) {
	i8254x_device_t *i8254x = device_priv(device);
	int err;

	err = bus_alloc_res(device, BUS_RES_INTR, 0, RF_NONE, &i8254x->intr);

	return err;
}

static int i8254x_detach(device_t *device) {
	i8254x_device_t *i8254x = device_priv(device);

	bus_free_res(device, &i8254x->intr);
	bus_free_res(device, &i8254x->regs);

	return 0;
}

static DEVTAB(i8254x_devtab, "i8254x", "pci") {
	{ .id = PCI_ID(PCIV_INTEL, PCI_ID_82540EM_A, PCI_CLASS_NET_ETH) },
	{ DEVTAB_END }
};

static DRIVER(i8254x_driver) {
	.name = "i8254x",
	.flags = DEV_DEP_INTR,
	.type = DEVICE_NET,
	.private = sizeof(i8254x_device_t),
	.devtab = &i8254x_devtab,
	.probe = i8254x_probe,
	.attach = i8254x_attach,
	.detach = i8254x_detach,
};

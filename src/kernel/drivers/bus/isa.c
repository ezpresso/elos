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
#include <drivers/bus/isa.h>

static int isa_alloc_res(device_t *device, bus_res_req_t *req) {
	if(req->type == BUS_RES_INTR) {
		/*
		 * ISA interrupts are edge triggered interrupts.
		 */
		assert(req->res->intr.flags == 0);
		req->res->intr.flags = BUS_TRIG_EDGE | BUS_POL_HI;
	}

	return bus_generic_alloc_res(device, req);
}

static int isa_probe(__unused device_t *device) {
	return DEVICE_PROBE_LOPRIO;
}

static int isa_attach(device_t *device) {
	device_set_desc(device, "ISA-bus");

	/*
	 * There is no real way of probing devices on an ISA bus
	 * (and I don't have any hardware using PnP-Bios), so let's
	 * just rely on the mboard driver.
	 */
	bus_probe_children(device);

	return 0;
}

static bus_driver_t isa_bus_driver = {
	.bus_alloc_res = isa_alloc_res,
	.bus_free_res = bus_generic_free_res,
	.bus_setup_res = bus_generic_setup_res,
	.bus_teardown_res = bus_generic_teardown_res,
};

static DRIVER(isa_drv) {
	.name = "isa",
	.flags = 0,
	.type = DEVICE_BUS,
	.probe = isa_probe,
	.attach = isa_attach,
	.detach = bus_generic_detach,
	.new_pass = bus_generic_new_pass,
	.bus = &isa_bus_driver,
};

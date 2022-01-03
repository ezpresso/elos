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
#include <drivers/bus/pci.h>

/*
 * PCI host bridge driver
 *
 * PCI tree example:
 * rootdev
 *   mboard
 *      northb
 *        pci-host
 *          dev
 *          dev
 *          bridge
 *             dev
 *          pci-isa-bridge
 *             isa-bus
 *               i8259
 *               atrtc
 *               atpit
 *               uart-pc
 *               uart-pc
 *
 * Since the host bridges show up as PCI devices on the bus,
 * the real host-bridge pci-device will additionaly be child of pci-host
 * (and a driver may drive this device). I'll probably have to change this...
 */

static int pci_host_get_bus(__unused device_t *device) {
	/*
	 * TODO actually somehow obtain the bus number of the root
	 * PCI bus.
	 */
	return 0;
}

static int pci_host_probe(__unused device_t *device) {
	return DEVICE_PROBE_OK;
}

static int pci_host_attach(device_t *device) {
	int busnum;

	busnum = pci_host_get_bus(device);
	if(busnum < 0) {
		return busnum;
	}

	device_set_desc(device, "Host-PCI bridge");
	pci_bus_probe(device, busnum);
	bus_probe_children(device);

	return 0;
}

#if notyet
static DEVTAB(pci_host_devtab, "pci-host", "isa") {
	{ .id = ISA_ID(ISA_VEN_PNP, 0x0A08) /* PCIe root bridge */ },
	{ .id = ISA_ID(ISA_VEN_PNP, 0x0A03) /* Compatible PCI root bridge */ },
	{ DEVTAB_END }
};
#endif

static bus_driver_t pci_host_bus_driver = {
	.bus_child_detach = NULL,
	.bus_child_free = pci_generic_child_free,
	.bus_alloc_res = pci_generic_alloc_res,
	.bus_free_res = bus_generic_free_res,
	.bus_setup_res = bus_generic_setup_res,
	.bus_teardown_res = bus_generic_teardown_res,
};

static DRIVER(pci_host_drv) {
	.name = "pci-host",
	.flags = 0,
	.type = DEVICE_BUS,
	.probe = pci_host_probe,
	.attach = pci_host_attach,
	.detach = bus_generic_detach,
	.new_pass = bus_generic_new_pass,
	.bus = &pci_host_bus_driver,
};

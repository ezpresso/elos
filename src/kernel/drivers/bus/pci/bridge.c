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

static int pci_bridge_probe(__unused device_t *device) {
	return DEVICE_PROBE_LOPRIO;
}

static int pci_bridge_attach(device_t *device) {
	pci_bridge_t *bridge = device_priv(device);


	device_set_desc(device, "PCI-to-PCI bridge");
	bridge->primary = pci_cfg_readb(device, PCI_P2PCFG_PRIM);
	bridge->secondary = pci_cfg_readb(device, PCI_P2PCFG_SEC);
	bridge->subordinate = pci_cfg_readb(device, PCI_P2PCFG_SUB);

	/*
	 * Allocate the resources this bridge is using.
	 */
#if 0
	kprintf("bus:%d\n", bridge->secondary);
	kprintf("\t0x%x - 0x%x ", pci_cfg_readb(device, PCI_BRIDGE_IO_BASE)<<8,
		pci_cfg_readb(device, PCI_BRIDGE_IO_LIMIT)<<8);

	kprintf(" 0x%x - 0x%x\n", pci_cfg_readw(device, PCI_BRIDGE_MEM_BASE)<<16,
		pci_cfg_readw(device, PCI_BRIDGE_MEM_LIMIT)<<16);
#endif
	
	pci_bus_probe(device, bridge->secondary);
	bus_probe_children(device);

	return 0;
}

static DEVTAB(pci_bridge_devtab, "pci-bridge", "pci") {
	{ .id = PCI_ID(PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_BRIDGE_P2P) },
	{ DEVTAB_END },
};

static bus_driver_t pci_bridge_bus_driver = {
	.bus_child_detach = NULL,
	.bus_child_free = pci_generic_child_free,
	.bus_alloc_res = pci_generic_alloc_res,
	.bus_free_res = bus_generic_free_res,
	.bus_setup_res = bus_generic_setup_res,
	.bus_teardown_res = bus_generic_teardown_res,
};

static DRIVER(pci_bridge_drv) {
	.name = "pci-bridge",

	/*
	 * TODO allocate IO / MEM windows dynamically (i.e. ignore FW
	 * settings) ?
	 */
	.flags = 0,
	.type = DEVICE_BUS,
	.private = sizeof(pci_bridge_t),
	.devtab = &pci_bridge_devtab,
	.probe = pci_bridge_probe,
	.attach = pci_bridge_attach,
	.detach = bus_generic_detach,
	.new_pass = bus_generic_new_pass,
	.bus = &pci_bridge_bus_driver,
};

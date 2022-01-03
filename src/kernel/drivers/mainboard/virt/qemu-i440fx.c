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
#include <device/mainboard.h>
#include <device/tree.h>
#include <drivers/bus/pci.h>
#include <drivers/acpi.h>

/*
 * QEMU motherboard driver with i440fx chipset.
 */
static mboard_driver_t qemu_i440fx_mboard;
static device_t *isa_bridge;

static int qemu_i440fx_probe(__unused device_t *device) {
	if(mboard_present()) {
		return DEVICE_PROBE_FAIL;
	} else {
		return DEVICE_PROBE_OK;
	}
}

static int qemu_i440fx_pci_intr_route(device_t *device, device_t *bus,
	uint8_t slot, uint8_t pin, bus_res_req_t *req)
{
	pci_pirq_route_t route;
	uint8_t pirq;
	int err;

	(void) bus;
	assert(device_attached(isa_bridge));

	/*
	 * First calculate the PIRQ of the PCI interrupt pin and
	 * then ask the southbridge to map this value into an
	 * interrupt. Normally the PIRQ and southbridge stuff
	 * does not apply to the I/O-APIC, but with a QEMU
	 * i440fx (not q35) machine, the interrupts are
	 * equal for both controllers (which would never occur
	 * on real hw as far as I know).
	 */
	pirq = ((pin - 1) + (slot - 1)) & 3;
	device_printf(device, "routed slot 0x%x + INT%c -> PIRQ%c\n",
		slot, 'A' + pin - 1, 'A' + pirq);

	route.slot = slot;
	route.pirq = pirq;
	err = device_mthd(isa_bridge, DEVICE_PCI_PIRQ_ROUTE, &route);
	if(!err) {
		req->bus_id = 0;
		req->addr = route.irq;
		req->end = route.irq;
		req->size = 1;

		device_printf(device, "interrupt: PCI config: %d, manually "
			"routed: %d\n", pci_cfg_readb(
			bus_res_get_device(req->res), PCI_DEVCFG_INT_LINE),
			route.irq);
	} else {
		device_printf(device, "could not route PCI interrupt: %d\n",
			err);
	}

	return err;
}

/**
 * @brief Create the device tree.
 */
static void qemu_i440fx_tree(device_t *device) {
	acpi_attach_madt(device);

	/*
	 * TODO
	 * This is hardcoded for now.
	 */
	devtree_define(device,
		device("i440fx-pmc", BUS_DEV_ADDR_ANY) {
			device("pci-host", BUS_DEV_ADDR_ANY) {
				device("piix-isa", PCI_DEVFN(0x01, 0x00)) {
					isa_bridge = devtree_cur;

					device("isa", BUS_DEV_ADDR_ANY) {
						device("atrtc", BUS_DEV_ADDR_ANY) {
							io("io", 0x70, 0x08);
							intr("intr", 0x08, 0x01);
						}

						device("atpit", BUS_DEV_ADDR_ANY) {
							io("io", 0x40, 0x04);
							intr("intr", 0x00, 0x01);
						}

						device("i8042", BUS_DEV_ADDR_ANY) {
							io("io-cmd", 0x64, 0x01);
							io("io-data", 0x60, 0x01);
							intr("intr-port0", 0x01, 0x01);
							intr("intr-port1", 0x0c, 0x01);
						}

						device("i8259a", BUS_DEV_ADDR_ANY) {
							io("io-master", 0x20, 0x2);
							io("io-slave", 0xA0, 0x2);
							io("io-elcr", 0x4D0, 0x2);
							intr_parent(root_device);
						}

						device("uart-pc", BUS_DEV_ADDR_ANY) {
							io("io", 0x03F8, 0x08);
							intr("intr", 0x4, 0x01);
						}

						device("uart-pc", BUS_DEV_ADDR_ANY) {
							io("io", 0x02F8, 0x08);
							intr("intr", 0x3, 0x01);
						}
					}

					device("hpet", BUS_DEV_ADDR_ANY) {
						mem("regs", 0xfed00000, 0x1000);
					}
				}
			}
		}
	);
}

static int qemu_i440fx_attach(device_t *device) {
	qemu_i440fx_tree(device);
	mboard_register(device, &qemu_i440fx_mboard);

	device_set_desc(device, "QEMU mainboard (i440FX + PIIX)");
	bus_probe_children(device);

	return 0;
}

static int qemu_i440fx_detach(device_t *device) {
	int err;

	err = bus_generic_detach(device);
	if(!err) {
		mboard_unregister(device);
	}

	return err;
}

static mboard_driver_t qemu_i440fx_mboard = {
	.pci_irq_route = qemu_i440fx_pci_intr_route,
};

static bus_driver_t qemu_i440fx_bus_driver = {
	.bus_child_detach = NULL,
	.bus_alloc_res = bus_generic_alloc_res,
	.bus_free_res = bus_generic_free_res,
	.bus_setup_res = bus_generic_setup_res,
	.bus_teardown_res = bus_generic_teardown_res,
};

static DRIVER(qemu_i440fx_driver) {
	.name = "qemu-i440fx",
	.devtab = NULL,
	.flags = 0,
	.type = DEVICE_CHIPSET,
	.probe = qemu_i440fx_probe,
	.attach = qemu_i440fx_attach,
	.detach = qemu_i440fx_detach,
	.new_pass = bus_generic_new_pass,
	.bus = &qemu_i440fx_bus_driver,
};

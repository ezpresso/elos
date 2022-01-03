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
#include <device/mainboard.h>
#include <arch/pci.h>

#define pci_device(device) ((pci_device_t *)bus_child_priv(device))

/*
 * The pci device interface.
 */
static uint32_t pdev_cfg_read(pci_device_t *pdev, uint16_t off, uint8_t size) {
	return arch_pci_read(pdev->bus, pdev->devfn, off, size);
}

static void pdev_cfg_write(pci_device_t *pdev, uint16_t off, uint32_t value,
	uint8_t size)
{
	arch_pci_write(pdev->bus, pdev->devfn, off, size, value);
}

static void pci_cmd_set(device_t *device, uint16_t mask) {
	pci_cfg_writew(device, PCI_CFG_CMD, pci_cfg_readw(device, PCI_CFG_CMD) |
		mask);
}

#if 0
static void pci_cmd_clear(device_t *device, uint16_t mask) {
	pci_cfg_writew(device, PCI_CFG_CMD, pci_cfg_readw(device, PCI_CFG_CMD) &
		~mask);
}
#endif

static void pci_enable_io(device_t *device) {
	pci_cmd_set(device, PCI_CMD_IO);
}

static void pci_enable_mem(device_t *device) {
	pci_cmd_set(device, PCI_CMD_IO);
}

uint32_t pci_cfg_read(device_t *device, uint16_t off, uint8_t size) {
	return pdev_cfg_read(pci_device(device), off, size);
}

void pci_cfg_write(device_t *device, uint16_t off, uint32_t value,
	uint8_t size)
{
	pdev_cfg_write(pci_device(device), off, value, size);
}

uint8_t pcidev_slot(device_t *device) {
	return PCI_SLOT(pci_device(device)->devfn);
}

uint8_t pcidev_func(device_t *device) {
	return PCI_FUNC(pci_device(device)->devfn);
}

uint16_t pcidev_device(device_t *device) {
	return pci_device(device)->device;
}

uint16_t pcidev_vendor(device_t *device) {
	return pci_device(device)->vendor;
}

bool pcidev_busmaster(device_t *device) {
	pci_device_t *pdev = pci_device(device);

	pci_cmd_set(device, PCI_CMD_BUSMASTER);
	return !!(pdev_cfg_read(pdev, PCI_CFG_CMD, 2) & PCI_CMD_BUSMASTER);
}

size_t pci_bar_number(device_t *device) {
	pci_device_t *pdev = bus_child_priv(device);
	uint8_t hdr, num;

	hdr = pdev->hdr_type & PCI_HDR_TYPE_MASK;

	/*
	 * A PCI-PCI-bridge has only 2 bars.
	 */
	if(hdr == PCI_HDR_TYPE_PCI_BRIDGE) {
		num = PCI_P2PCFG_BAR_NUM;
	} else if(hdr == PCI_HDR_TYPE_NORMAL) {
		num = PCI_DEVCFG_BAR_NUM;
	} else {
		num = 0;
	}

	return num;
}

static void pci_read_bar(device_t *device, int bar, bus_res_type_t *type,
	uint64_t *addr, uint64_t *size)
{
	const uint8_t bar_cfg = PCI_DEVCFG_BAR(bar);
	uint64_t bar_value, bar_tmp;
	uint16_t cmd, tmp;
	int shift;
	bool bit64;

	/*
	 * Read the base address register.
	 */
	bar_value = pci_cfg_read(device, bar_cfg, 4);
	if(bar_value & PCI_BAR_TYPE_64) {
		bar_value |= ((uint64_t)pci_cfg_read(device, bar_cfg + 1, 4)
			<< 32);
		bit64 = true;
	} else {
		bit64 = false;
	}

	/*
	 * Disable memory / port.
	 */
	cmd = pci_cfg_read(device, PCI_CFG_CMD, 2);
	if(bar_value & PCI_BAR_IO) {
		tmp = cmd & ~PCI_CMD_IO;
		*type = BUS_RES_IO;
	} else {
		tmp = cmd & ~PCI_CMD_MEMORY;
		*type = BUS_RES_MEM;
	}
	pci_cfg_write(device, PCI_CFG_CMD, tmp, 2);

	/*
	 * Get the size of the bar by writing setting each bit of the field and
	 * reading the value back. That's how one can determine which bits are
	 * implemented by the device.
	 */
	pci_cfg_write(device, bar_cfg, UINT32_MAX, 4);
	bar_tmp = pci_cfg_read(device, bar_cfg, 4);
	pci_cfg_write(device, bar_cfg, bar_value, 4);

	if(bit64) {
		pci_cfg_write(device, bar_cfg + 1, UINT32_MAX, 4);
		bar_tmp |= (uint64_t)pci_cfg_read(device, bar_cfg + 1, 4) << 32;
		pci_cfg_write(device, bar_cfg + 1, bar_value>>32, 4);
	}

	/*
	 * Restore cmd.
	 */
	pci_cfg_write(device, PCI_CFG_CMD, cmd, 2);

	/*
	 * Calculate the size based on what the config returned.
	 */
	if(bar_value & PCI_BAR_IO) {
		*addr = PCI_BAR_ADDR_IO(bar_value);
		bar_tmp = PCI_BAR_ADDR_IO(bar_tmp);
	} else {
		*addr = PCI_BAR_ADDR_MEM(bar_value);
		bar_tmp = PCI_BAR_ADDR_MEM(bar_tmp);
	}

	/*
	 * Calculate the size of the bar once 1's where written to every bit in
	 * the bar and the value was read back.
	 * Cannot simply use (~testval + 1) here, which does work for example
	 * with an IO bar where _testval_ is 0xfffffff1 (=> size = 0x10), bit
	 * it does not work when _testval_ is 0x0000fff1.
	 */
	shift = ffs(bar_tmp);
	if(shift == 0) {
		*size = 0;
		*addr = 0;
		return;
	}

	*size = 1ULL << (shift - 1);
}

static bool pci_match_ids(uint64_t dev_id, uint64_t devtab_id) {
	uint16_t id_ven, id_dev, id_cls;

	id_ven = PCI_ID_VEN(devtab_id);
	id_dev = PCI_ID_DEV(devtab_id);
	id_cls = PCI_ID_CLS(devtab_id);

	if((id_ven == PCI_ANY_ID || (id_ven == PCI_ID_VEN(dev_id))) &&
	   (id_dev == PCI_ANY_ID || (id_dev == PCI_ID_DEV(dev_id))) &&
	   (id_cls == PCI_ANY_ID || (id_cls == PCI_ID_CLS(dev_id))))
	{
		return true;
	}

	return false;
}

/*
 * Scan every function of a PCI device.
 */
static void pci_bus_probe_funcs(device_t *parent, uint8_t bus, uint8_t devnum) {
	uint8_t hdr_type, nfunc, func, devfn;
	pci_device_t *pcidev;
	device_t *device;
	uint16_t class;
	uint32_t ven;

	hdr_type = arch_pci_read(bus, PCI_DEVFN(devnum, 0x00),
		PCI_CFG_HDR_TYPE, 1);
	if(hdr_type & PCI_HDR_TYPE_MULT_FUNC) {
		nfunc = 8;
	} else {
		nfunc = 1;
	}

	for(func = 0; func < nfunc; func++) {
		devfn = PCI_DEVFN(devnum, func);

		ven = arch_pci_read(bus, devfn, PCI_CFG_VENDOR, 4);
		if(ven == 0xffffffff || ven == 0x00000000 ||
			ven == 0x0000ffff || ven == 0xffff0000)
		{
			continue;
		}

		class = PCI_GEN_CLASS(
			arch_pci_read(bus, devfn, PCI_CFG_CLASS, 1),
			arch_pci_read(bus, devfn, PCI_CFG_SUBCLASS, 1));
		if(class == PCI_CLASS_BRIDGE_HOST) {
			/*
			 * The host bridge is parent of this device so don't
			 * add it as an child!
			 */
			continue;
		}

		/*
		 * The motherboard driver may already createt the device, so
		 * check if the device is already present before allocating
		 * a new one.
		 */
		if((device = bus_get_child(parent, devfn)) == NULL) {
			device = bus_add_child_at(parent, NULL, devfn);
		}

		pcidev = bus_alloc_child_priv(device, sizeof(*pcidev));
		pcidev->bus = bus;
		pcidev->devfn = devfn;
		pcidev->vendor = ven & 0xffff;
		pcidev->device = (ven >> 16) & 0xffff;
		pcidev->hdr_type = pdev_cfg_read(pcidev, PCI_CFG_HDR_TYPE, 1);
		pcidev->class = class;

		device_set_desc(device, "pci%02x:%02x:%02x (0x%04x:0x%04x, "
			"%02x:%02x)", pcidev->bus, PCI_SLOT(pcidev->devfn),
			PCI_FUNC(pcidev->devfn), pcidev->vendor, pcidev->device,
			PCI_CLASS(pcidev->class), PCI_SUBCLASS(pcidev->class));

		/*
		 * Give the device manager some information about the device,
		 * so the appropriate driver can be found.
		 */
		device_add_id(device, "pci", PCI_ID(pcidev->vendor,
			pcidev->device, pcidev->class), pci_match_ids);
	}
}

void pci_bus_probe(device_t *device, uint8_t bus) {
	device_t *child;
	uint8_t dev;

	for(dev = 0; dev < PCI_DEVICE_NUM; dev++) {
		pci_bus_probe_funcs(device, bus, dev);
	}

	/*
	 * Check that the mainboard driver did not add any devices we
	 * did not find while enumerating the bus.
	 */
	 bus_foreach_child(child, device) {
		 if(bus_child_priv(child) == NULL) {
			 bus_dev_addr_t addr = device_addr(child);

			 kpanic("[pci] warning: unknown device in the tree: "
			 	"0x%02x:0x%04llx (%s)", bus, addr,
				device_name(device));
		 }
	 }
}

void pci_generic_child_free(__unused device_t *device, device_t *child) {
	bus_free_child_priv(child);
}

static int pci_alloc_bar(device_t *device, device_t *child,
	bus_res_req_t *req)
{
	bus_res_type_t bar_type;
	uint64_t bar_addr, bar_size;
	size_t bar;
	int err;

	if(req->bus_id < PCI_RES_BAR(0) ||
		req->bus_id >= PCI_RES_BAR(PCI_DEVCFG_BAR_NUM))
	{
		return -EINVAL;
	}

	bar = PCI_BUS_ID_TO_BAR(req->bus_id);
	if(bar >= pci_bar_number(child)) {
		return -EINVAL;
	}

	/*
	 * Get the size and address of the bar.
	 */
	pci_read_bar(child, bar, &bar_type, &bar_addr,
		&bar_size);

	/*
	 * PCI supports 64bit addresses, however the
	 * system might be running in 32bit mode.
	 */
	if(BUS_ADDR_SUPP(bar_addr, bar_size)) {
		return -ENOTSUP;
	}

	/*
	 * No bar present or wrong bar type.
	 */
	if(bar_addr == 0 || bar_type != req->type) {
		return -EINVAL;
	}

	req->addr = bar_addr;
	req->end = bar_addr - 1 + bar_size;
	req->size = bar_size;
	req->align = 0x1;
	req->bus_id = 0;

	err = bus_generic_alloc_res(device, req);
	if(!err) {
		/* Enable the resource.
		 * TODO I'd like to put this into map/unmap,
		 * however there might be problems:
		 *	- multiple MEM resources
		 *	- multiple IO resource
		 * 	- how does the setup/teardown callback know that the
		 * 	 resource is a PCI BAR?
		 */
		if(req->type == BUS_RES_IO) {
			pci_enable_io(child);
		} else {
			pci_enable_mem(child);
		}
	}

	return err;
}

static int pci_alloc_intr(device_t *device, device_t *child,
	bus_res_req_t *req)
{
	pci_device_t *pdev = pci_device(child);
	uint8_t pin, slot;
	int err;

	if(req->bus_id != 0) {
		kpanic("TODO MSI / INTx");
	}

	pin = pci_cfg_readb(child, PCI_DEVCFG_INT_PIN);
	if(!pin) {
		return -EINVAL;
	}

	/*
	 * The motherboard developers are free to choose to which interrupt
	 * on the interrupt controller(s) a pci interrupt pin is routed. The
	 * PCI driver does not know where the interrupt is delivered to,
	 * we just ask the motherboard driver.
	 */
	slot = PCI_SLOT(pdev->devfn);
	err = mboard_pci_irq_route(device, slot, pin, req);
#if notyet
	if(err == whatever) {
		req->bus_id = PCI_RES_INTPIN;

		/*
		 * Calculate the parent pin based on the default
		 * PCI interrupt swizzle algorithm if there is
		 * no other information about the PCI-PCI bridge.
		 * TODO assumes _device_ != pci_root
		 */
		req->addr = pci_int_swizzle(slot, pin);
		req->end = req->addr;
		req->size = 1;
		return bus_generic_alloc_res(device, req);
	} else
#endif
	if(err) {
		return err;
	}

	/*
	 * PCI interrupts are level triggered and active high.
	 */
	req->res->intr.flags = BUS_TRIG_LVL | BUS_POL_HI;

	return bus_generic_alloc_res(device, req);
}

int pci_generic_alloc_res(device_t *device, bus_res_req_t *req) {
	device_t *child = bus_res_get_device(req->res);

	if(!BUS_ADDR_ANY(req->addr, req->end, req->size) ||
		device_get_parent(child) != device)
	{
		return bus_generic_alloc_res(device, req);
	}

	/*
	 * Allocate memory/io bar.
	 */
	if(req->type == BUS_RES_IO || req->type == BUS_RES_MEM) {
		return pci_alloc_bar(device, child, req);
	} else {
		assert(req->type == BUS_RES_INTR);
		return pci_alloc_intr(device, child, req);
	}
}

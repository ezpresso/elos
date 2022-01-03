#ifndef DEVICE_BUS_PCI_H
#define DEVICE_BUS_PCI_H

#include <device/device.h>
#include <drivers/bus/pcipriv/pciid.h>
#include <drivers/bus/pcipriv/pcireg.h>

#define pci_cap_foreach(dev, offset, id) \
	for((offset) = (pci_cfg_readb(dev, PCI_CFG_STATUS) & PCI_STATUS_CAP_LIST) ? \
		({ uint8_t _tmp = pci_cfg_readb(dev, PCI_DEVCFG_CAP_PTR); \
			(id) = pci_cfg_readb(dev, _tmp); _tmp; }) : PCI_CAP_END; \
		(offset) != PCI_CAP_END; \
		(offset) = pci_cfg_readb(dev, offset + 1), \
		(id) = pci_cfg_readb(dev, offset))


#define PCI_DEVICE_NUM 32					

#define PCI_SLOT(devfn) \
	(((devfn) >> 3) & 0x1f)

#define PCI_FUNC(devfn) \
	((devfn) & 0x07)

#define PCI_DEVFN(slot, func) \
	((((slot) & 0x1f) << 3) | ((func) & 0x07))

#define PCI_ACPI_ADDR(devfn) \
	(uint32_t)((PCI_SLOT(devfn) << 16) | PCI_FUNC(devfn))

typedef struct pci_bridge {
	uint8_t primary;
	uint8_t secondary;

	/*
	 * The bus number of the highest numbered PCI bus
	 * segment subordinate to the bridge.
	 */
	uint8_t subordinate;
} pci_bridge_t;

typedef struct pci_device {
	uint8_t bus;
	uint16_t vendor;
	uint16_t device;
	uint16_t class;
	uint8_t devfn;
	uint8_t hdr_type;
	uint8_t int_pin;
} pci_device_t;

/**
 * @brief Detect and attach child devices on a PCI bus.
 */
void pci_bus_probe(device_t *device, uint8_t bus);

/**
 * @brief Get the number of bars for a device.
 *
 * P2P bridges only have 2 BAR registers and "normal" pci devices
 * habe 6 of them.
 */
size_t pci_bar_number(device_t *device);

/**
 * @brief The default implementation of the bus_child_free driver callback.
 */
void pci_generic_child_free(device_t *device, device_t *child);

/**
 * @brief The default implementation of the bus_alloc_res driver callback.
 */
int pci_generic_alloc_res(device_t *device, bus_res_req_t *req);

/**
 * Apply the PCI swizzle algorithm to get the pin number of the
 * parent bridge (only valid for PCI-PCI bridges in add-in cards).
 * If PCI-interrupt routing would be always so easy....
 *
 * @param pin the pin number of the device (1 - 4)
 * @param dev the pci device number
 * @return the pin number of the parent (1 - 4)
 */
static inline int pci_int_swizzle(int dev, int pin) {
	return (((pin - 1) + dev) % 4) + 1;
}

/* pci device interface */

/* numbers for bus_id (bus_alloc_res) */
/* ids for io and memory resources */
#define PCI_RES_BAR(x)		((x) + 1)
#define PCI_BUS_ID_TO_BAR(x)	((x) - 1)
#define PCI_RES_VGA		PCI_RES_BAR(PCI_DEVCFG_BAR_NUM)
/* ids for memory resources only */
#define PCI_RES_ROM		(PCI_RES_VGA + 1)

#if 0
/*
 * TODO
 * Device drivers allocate their pci INTx interrupts
 * with bus_id being set to 0 using bus_alloc_res!
 * Internal routing sets bus_id to PCI_RES_INTPIN
 * and addr/end to PCI_PIN_INT[A-D] and size to 1
 */
#define PCI_RES_INTPIN	1
#endif

#define pci_cfg_readb(d, o) pci_cfg_read(d, o, 1)
#define pci_cfg_readw(d, o) pci_cfg_read(d, o, 2)
#define pci_cfg_readl(d, o) pci_cfg_read(d, o, 4)
#define pci_cfg_writeb(d, o, v) pci_cfg_write(d, o, v, 1)
#define pci_cfg_writew(d, o, v) pci_cfg_write(d, o, v, 2)
#define pci_cfg_writel(d, o, v) pci_cfg_write(d, o, v, 4)
uint32_t pci_cfg_read(device_t *device, uint16_t off, uint8_t size);
void pci_cfg_write(device_t *device, uint16_t off, uint32_t value,
	uint8_t size);

uint8_t pcidev_slot(device_t *device);
uint8_t pcidev_func(device_t *device);
uint16_t pcidev_device(device_t *device);
uint16_t pcidev_vendor(device_t *device);
bool pcidev_busmaster(device_t *device);

#endif
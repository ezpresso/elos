#ifndef DEVICE_MAINBOARD_H
#define DEVICE_MAINBOARD_H

typedef struct mboard_driver {
	int (*pci_irq_route) (device_t *device, device_t *pcibus, uint8_t slot,
		uint8_t pin, bus_res_req_t *req);
} mboard_driver_t;

int mboard_pci_irq_route(device_t *bus, uint8_t slot, uint8_t pin,
	bus_res_req_t *req);

bool mboard_present(void);
void mboard_register(device_t *device, mboard_driver_t *driver);
void mboard_unregister(device_t *device);

#endif
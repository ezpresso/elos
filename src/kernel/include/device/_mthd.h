#ifndef DEVICE_MTHD_H
#define DEVICE_MTHD_H

typedef struct pci_pirq_route {
	int slot;
	int pirq;
	int irq;
} pci_pirq_route_t;

enum {
	DEVICE_PCI_PIRQ_ROUTE = 0,
};

#endif
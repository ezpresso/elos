#ifndef DRIVERS_DISK_ATA_PCI_H
#define DRIVERS_DISK_ATA_PCI_H

#include <drivers/disk/ata.h>

#define ATA_NCHAN 2 /* Not true in every case, but ... */

typedef struct ata_pci_cntlr {
	ata_cntlr_t cntlr;
	bus_res_t bmi; /* bus master interface */
	ata_chan_t channels[ATA_NCHAN];
} ata_pci_cntlr_t;

#endif
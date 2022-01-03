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
#include <drivers/bus/pci.h>
#include <drivers/disk/ata.h>
#include <drivers/disk/ata-pci.h>

#define BMIBA PCI_RES_BAR(4)

/*
 * Add BM_SCND to the register offset for secondary controller.
 */
#define BM_SCND		0x08
#define BMICOM		0x00
#define 	SSBM 		(1 << 0) /* start (=1) / stop (=0) busmaster */
/*
 * read (=0) / write (=1), do not change while while BM function is active.
 */
#define 	RWCON		(1 << 3)
#define BMISTA		0x02
#define 	DMA1CAP		(1 << 6)
#define		DMA0CAP		(1 << 5)
#define 	INTR_STA	(1 << 2)
#define		DMA_ERR		(1 << 1)
#define 	BMIDEA		(1 << 0) /* active */
/*
 * address of the descriptor table; dword alignment;
 */
#define BMIDTP		0x04
#define ATA_PRIM_IRQ	(14)
#define ATA_SEC_IRQ	(15)

static inline uint8_t ata_bmi_offset(ata_chan_t *chan) {
	return chan->idx != 0 ? BM_SCND : 0;
}

static void ata_pci_dma_prep(ata_chan_t *chan) {
	ata_pci_cntlr_t *cntlr = chan->cntlr->priv;
	uint8_t off = ata_bmi_offset(chan);

	/*
	 * TODO I don't think this has to be done in every prep().
	 */
	bus_res_writel(&cntlr->bmi, BMIDTP + off, chan->prdt_addr);

	/*
	 * When reading from device, the device has to write to memory.
	 */
	bus_res_writeb(&cntlr->bmi, BMICOM + off,
		chan->tf.dma_dir == ATA_DMA_TO_CPU ? RWCON : 0);

	/*
	 * Clear err / irq status.
	 */
	bus_res_writeb(&cntlr->bmi, BMISTA + off, bus_res_readb(&cntlr->bmi,
		BMISTA + off) | INTR_STA | DMA_ERR);
}

static void ata_pci_dma_start(ata_chan_t *chan) {
	ata_pci_cntlr_t *cntlr = chan->cntlr->priv;
	bus_res_writeb(&cntlr->bmi, BMICOM + ata_bmi_offset(chan),
		(chan->tf.dma_dir == ATA_DMA_TO_CPU ? RWCON : 0) | SSBM);
}

static void ata_pci_dma_done(ata_chan_t *chan) {
	ata_pci_cntlr_t *cntlr = chan->cntlr->priv;
	uint8_t sta, off = ata_bmi_offset(chan);

	/*
	 * Clear start/stop bit to stop the dma.
	 */
	bus_res_writeb(&cntlr->bmi, BMICOM + off, 0);

	/*
	 * Clear err / irq status.
	 */
	sta = bus_res_readb(&cntlr->bmi, BMISTA + off);
	bus_res_writeb(&cntlr->bmi, BMISTA + off, sta | INTR_STA | DMA_ERR);

	chan->tf.dma_err = !!(sta & DMA_ERR);
}

static int ata_pci_alloc_res(device_t *device, bus_res_req_t *req) {
	assert(req->bus_id == 0);
	if(req->type == BUS_RES_INTR) {
		assert(req->size == 1);
		assert(!BUS_ADDR_ANY(req->addr, req->end, req->size));

		/*
		 * Those interrupts are ISA interrupts and not PCI interrupts,
		 * because we are operating in compat mode. TODO
		 */
		req->res->intr.flags = BUS_TRIG_EDGE | BUS_POL_HI;
	}

	return bus_generic_alloc_res(device, req);
}

static void ata_pci_on_child_free(__unused device_t *device, device_t *child) {
	ata_chan_t *chan = bus_child_priv(child);

	if(chan->cntlr->flags & ATA_CNTLR_DMA) {
		bus_dma_destroy_buf(&chan->prdt_dma);
	}

	bus_set_child_priv(child, NULL);
}

/**
 * @brief Check whether the device is in compat mode or not.
 */
static bool ata_pci_compat(device_t *device) {
	return (pci_cfg_readb(device, PCI_CFG_PROG_IF) &
			(PCI_IDE_PRIM_MODE | PCI_IDE_SEC_MODE)) == 0;
}

static int ata_pci_probe(__unused device_t *device) {
	/*
	 * TODO currently not supported.
	 */
	if(!ata_pci_compat(device)) {
		return DEVICE_PROBE_FAIL;
	}

	/*
	 * Chipset specific driver should be preferred.
	 */
	return DEVICE_PROBE_LOPRIO;
}

static void ata_pci_add_channel(device_t *device, ata_pci_cntlr_t *cntlr,
	size_t idx, uint16_t io, uint8_t intr)
{
	ata_chan_t *chan;
	device_t *child;

	chan = &cntlr->channels[idx];
	chan->idx = idx;
	chan->cntlr = &cntlr->cntlr;

	/*
	 * Create a dma buffer for the prdt entries.
	 */
	if(cntlr->cntlr.flags & ATA_CNTLR_DMA) {
		bus_dma_create_buf(device,
				0, /* start */
				BUS_MAX32, /* end*/
				4, /* alignment */
				64 << KB_SHIFT, /* boundary */
				1, /* max number of segments */
				BUS_MAX16, /* segsz max */
				0, /* flags */
				&chan->prdt_dma
			);
	}

	child = bus_add_child_at(device, "ata-channel", idx);
	device_prop_add_res(child, "io",   BUS_RES_IO, io, ATA_IO_SZ);
	device_prop_add_res(child, "cntl", BUS_RES_IO, io + ATA_CTL_OFF,
		ATA_CTL_SZ);
	device_prop_add_res(child, "intr", BUS_RES_INTR, intr, 1);
	bus_set_child_priv(child, chan);
}

static int ata_pci_attach(device_t *device) {
	ata_pci_cntlr_t *cntlr = device_priv(device);
	int err;

	cntlr->cntlr.flags = 0;
	cntlr->cntlr.dma_prep = ata_pci_dma_prep;
	cntlr->cntlr.dma_start = ata_pci_dma_start;
	cntlr->cntlr.dma_done = ata_pci_dma_done;
	cntlr->cntlr.priv = cntlr;

	/*
	 * Try enabling bus-mastering.
	 */
	if(pcidev_busmaster(device)) {
		err = bus_alloc_res(device, BUS_RES_IO, BMIBA, RF_MAP,
			&cntlr->bmi);
		if(err == 0) {
			F_SET(cntlr->cntlr.flags, ATA_CNTLR_DMA);
		}
	}

	/*
	 * Register the channel devices.
	 */
	ata_pci_add_channel(device, cntlr, 0, ATA_IO_PRIM, ATA_PRIM_IRQ);
	ata_pci_add_channel(device, cntlr, 1, ATA_IO_SEC, ATA_SEC_IRQ);
	device_set_desc(device, "Generic PCI ATA controller");

	bus_probe_children(device);

	return 0;
}

static int ata_pci_detach(device_t *device) {
	ata_pci_cntlr_t *cntlr = device_priv(device);
	int err;

	err = bus_generic_detach(device);
	if(!err) {
		bus_free_res(device, &cntlr->bmi);
	}

	return err;
}

static DEVTAB(ata_pci_devtab, "ata-pci", "pci") {
	{ .id = PCI_ID(PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_IDE) },
	{ DEVTAB_END }
};

static bus_driver_t ata_pci_bus = {
	.bus_alloc_res = ata_pci_alloc_res,
	.bus_free_res = bus_generic_free_res,
	.bus_setup_res = bus_generic_setup_res,
	.bus_teardown_res = bus_generic_teardown_res,
	.bus_child_free = ata_pci_on_child_free,
};

static DRIVER(ata_pci_driver) {
	.name = "ata-pci",
	.flags = DEV_DEP_INTR,
	.type = DEVICE_DISK,
	.private = sizeof(ata_pci_cntlr_t),
	.devtab = &ata_pci_devtab,
	.probe = ata_pci_probe,
	.attach = ata_pci_attach,
	.detach = ata_pci_detach,
	.bus = &ata_pci_bus,
};

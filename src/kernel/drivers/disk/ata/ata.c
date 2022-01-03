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
#include <kern/endian.h>
#include <kern/time.h> /* wait_for_ms */
#include <kern/sched.h>
#include <vm/malloc.h>
#include <device/device.h>
#include <block/block.h>
#include <block/device.h>
#include <drivers/disk/ata.h>

/*
 * Time is in ms. I took those numbers from linux's ide.h
 */
#define ATA_WAIT_WORSTCASE	30000 /* 30s, worst case when spinning up */
/*
 * 10s, ATAPI device responds to 'identify' command
 */
#define	ATAPI_WAIT_ID		10000
#define ATA_WAIT_SEL 		30

/*
 * The channel structure is provided by parent.
 */
#define ata_channel(device) ((ata_chan_t *)bus_child_priv(device))

#define ata_readb(chan, off)	bus_res_readb(&(chan)->io, off)
#define ata_writeb(ch, off, v)	bus_res_writeb(&(ch)->io, off, v)

/**
 * @brief Write to the control register.
 */
#define ata_cntl(chan, val) bus_res_writeb(&(chan)->cntl, ATA_CTL, val)

/**
 * @brief Read the alternative status register.
 */
#define ata_altsta(chan) bus_res_readb(&(chan)->cntl, ATA_ALTSTA)


#if 0
static inline uint8_t ata_packet(ata_chan_t *chan, uint16_t *packet) {
	return ata_command(chan, ATA_CMD_PKT, packet, 6);
}

static void atapi_test(ata_chan_t *chan) {
	uint8_t last = 0;
	uint8_t packet[12], packet2[12];

	memset(packet, 0x00, sizeof(packet));
	memset(packet2, 0x00, sizeof(packet2));

	packet[0] = 0x25;
	packet2[0] = 0x03;
	//packet2[8]

	/* TODO check Mechanism Status */

	while(true) {
		/* Send packet command command */
		uint8_t sta = ata_packet(chan, (uint16_t *)packet);

		/* Get the error value */
		if(chan->intr_sta & ATA_S_ERR) {
			uint8_t err = ata_readb(chan, ATA_ERR);

			if(err & ATA_E_MC) {
				sta = ata_packet(chan, (uint16_t *)packet2);
				assert(!(sta & ATA_S_ERR));
			}

			if(last == 0) {
				kprintf("Eject\n");
				last = 1;
			}
		} else {
			if(last == 1) {
				kprintf("insert\n");
			}

			last = 0;
		}

		(void) bus_res_readl(&chan->io, ATA_DATA);
		(void) bus_res_readl(&chan->io, ATA_DATA);
	}
}
#endif

#if 0
static const char *ata_fix_str(uint16_t *ptr, size_t max) {
	char *str = (char *)ptr;
	size_t i;

	for(i = 0; i < max; i++) {
		ptr[i] = be16_to_cpu(ptr[i]);
	}

	/*
	 * Remove trailing spaces.
	 */
	for(i = max * 2 - 1; i > 0; i--) {
		if(str[i] == ' ' && str[i - 1] != ' ') {
			str[i] = '\0';
			break;
		}
	}

	return str;
}
#endif

static inline uint8_t ata_rw_dma_cmd(ata_dev_t *dev, bool wr, bool dma) {
	static const uint8_t cmds[2][2][2] = {
		{{ ATA_CMD_RD, ATA_CMD_WR }, { ATA_CMD_DMARD, ATA_CMD_DMAWR }},
		{{ ATA_CMD_RD48, ATA_CMD_WR48 }, { ATA_CMD_DMARD48,
			ATA_CMD_DMAWR48 }},
	};
	bool lba48 = !!(dev->flags & ATA_DEV_LBA48);
	return cmds[lba48][dma][wr];
}

static inline uint8_t ata_delay(ata_chan_t *chan) {
	for(int i = 0; i < 4; i++) {
		ata_altsta(chan);
	}

	return ata_altsta(chan);
}

static inline bool ata_bsy_wait(ata_chan_t *chan, int ms) {
	return wait_for_ms(!(ata_readb(chan, ATA_STA) & ATA_S_BSY), ms);
}

static inline bool ata_select(ata_dev_t *dev) {
	uint8_t val = ata_readb(dev->chan, ATA_DRIVE);
	bool res = true;

	if((val & ATA_D_SLAVE) != dev->drive) {
		ata_writeb(dev->chan, ATA_DRIVE, dev->drive | ATA_D_IBM);
		res = !(ata_delay(dev->chan) & ATA_S_BSY);
	}

	return res;
}

static inline uint8_t ata_poll(ata_chan_t *chan, uint8_t flags) {
	uint8_t sta;

	/*
	 * Wait until device is no longer busy.
	 */
	while(ata_readb(chan, ATA_STA) & ATA_S_BSY) {
		continue;
	}

	/*
	 * Wait for the correct flags or an error.
	 */
	do {
		sta = ata_readb(chan, ATA_STA);
	} while(!(sta & ATA_S_ERR) && (sta & flags) != flags);

	return sta;
}

static void ata_prep_dma(ata_chan_t *chan) {
	bus_dma_seg_t *seg;
	bus_size_t i;

	/*
	 * Initialize the prdt.
	 */
	foreach_dma_seg(&chan->dma, i, seg) {
		chan->prdt[i].addr = seg->addr;
		chan->prdt[i].count = seg->size;
		if(i == chan->dma.nseg - 1) {
			chan->prdt[i].flags = ATA_PRDT_EOT;
		}
	}

	/*
	 * Make sure that the device sees the prdt.
	 */
	bus_dma_sync(&chan->prdt_dma, BUS_DMA_SYNC_DEV);
	chan->cntlr->dma_prep(chan);
}

static void ata_finish_dma(ata_chan_t *chan) {
	chan->cntlr->dma_done(chan);
	bus_dma_unload(&chan->dma);
}

static uint8_t ata_cmd_start(ata_chan_t *chan, uint8_t cmd) {
	chan->tf.success = 0;
	chan->tf.cmd = cmd;

	/*
	 * Launch command.
	 */
	ata_writeb(chan, ATA_CMD, cmd);

	/*
	 * Feed the device with some data before going to sleep.
	 */
	if(chan->tf.out_buf) {
		uint8_t sta = ata_poll(chan, ATA_S_DRQ);
		if(sta & ATA_S_ERR || !(sta & ATA_S_DRQ)) {
			return sta;
		}

		/*
		 * Assume that the device supports dword I/O.
		 */
		for(size_t i = 0; i < (chan->tf.out_sz >> 2); i++) {
			bus_res_writel(&chan->io, ATA_DATA,
				chan->tf.out_buf[i]);
		}
	}

	/*
	 * Start the dma transfer if necessary.
	 */
	if(chan->tf.dma) {
		chan->cntlr->dma_start(chan);
	}

	return 0;
}

static int ata_cmd(ata_chan_t *chan, uint8_t cmd) {
	waiter_t wait;
	uint8_t res;

	wait_init_prep(&chan->intrwq, &wait);
	res = ata_cmd_start(chan, cmd);
	if(res & ATA_S_ERR) {
		wait_abort(&chan->intrwq, &wait);
		return -EIO;
	}
	wait_sleep(&chan->intrwq, &wait, 0);
	waiter_destroy(&wait);

	assert(!chan->tf.in_buf);
	assert(!chan->tf.out_buf);
	assert(!chan->tf.dma);

	return chan->tf.success ? 0 : -EIO;
}

static int ata_blk_io(ata_dev_t *dev, blk_req_t *req) {
	ata_chan_t *chan = dev->chan;
	uint8_t head, sta, cmd = 0;
	blkcnt_t nblk;
	blkno_t lba;
	int err;

	assert(!(dev->flags & ATA_DEV_ATAPI));
	lba = req->io.blk;
	nblk = req->io.cnt;

	/*
	 * 0 means 65536 sectors in LBA48 mode and 256 otherwise.
	 */
	if(nblk == dev->io_max_sect) {
		nblk = 0;
	}

	/*
	 * TODO DMA currently breaks lots of stuff and I don't know why.
	 */
	if(F_ISSET(dev->flags, ATA_DEV_DMA)) {
		err = bus_dma_load_blk(&chan->dma, req);
		if(err) {
			return -EIO;
		}

		chan->tf.dma = 1;
		if(req->type == BLK_WR) {
			/*
			 * Make sure that the device sees the data to be
			 * written.
			 */
			bus_dma_sync(&chan->dma, BUS_DMA_SYNC_DEV);
			chan->tf.dma_dir = ATA_DMA_TO_DEV;
		} else {
			chan->tf.dma_dir = ATA_DMA_TO_CPU;
		}

		ata_prep_dma(chan);
	} else {
		if(req->type == BLK_WR) {
			chan->tf.out_buf = (uint32_t *) req->io.map;
			chan->tf.out_sz =  req->io.cnt << ATA_SECT_LG;
		} else {
			chan->tf.in_buf = (uint32_t *) req->io.map;
			chan->tf.in_sz =  req->io.cnt << ATA_SECT_LG;
		}
	}

	cmd = ata_rw_dma_cmd(dev, req->type == BLK_WR, chan->tf.dma);
	head = dev->drive | ATA_D_IBM | ATA_D_LBA;

	/*
	 * Remember count 0 actually means 65536.
	 */
	if(dev->flags & ATA_DEV_LBA48) {
		ata_writeb(chan, ATA_COUNT,  (nblk >> 8) & 0xFF);
		ata_writeb(chan, ATA_SECT,   (lba >> 24) & 0xFF);
		ata_writeb(chan, ATA_CYL_LO, (lba >> 32) & 0xFF);
		ata_writeb(chan, ATA_CYL_HI, (lba >> 40) & 0xFF);
	} else {
		/*
		 * Highest 4 bits are stored here in LBA28 mode.
		 */
		head |= (lba >> 24) & 0xF;
	}

	ata_writeb(chan, ATA_COUNT, nblk & 0xFF);
	ata_writeb(chan, ATA_SECT, lba & 0xFF);
	ata_writeb(chan, ATA_CYL_LO, (lba >> 8) & 0xFF);
	ata_writeb(chan, ATA_CYL_HI, (lba >> 16) & 0xFF);
	ata_writeb(chan, ATA_DRIVE, head);

	/*
	 * Launch the command.
	 */
	sta = ata_cmd_start(chan, cmd);
	if(sta & ATA_S_ERR) {
		if(chan->tf.dma) {
			ata_finish_dma(chan);
		}
		return -EIO;
	}

	return 0;
}

static inline int ata_start_req(blk_dev_t *bdev, blk_req_t *req) {
	ata_dev_t *dev = blk_dev_priv(bdev);
	ata_chan_t *chan = dev->chan;
	int res;

	/*
	 * Can only handle one request at a time. The blk_req_queue and the
	 * block device manager will take care of this. The device will be able
	 * to handle requests after this request is finished.
	 */
	assert(chan->request == NULL);
	chan->request = req;

	if(ata_select(dev) == true) {
		switch(req->type) {
		case BLK_RD:
		case BLK_WR:
			res = ata_blk_io(dev, req);
			break;
		default:
			kpanic("[ata] unknown block command");
		}
	} else {
		res = -EIO;
	}

	if(res < 0) {
		chan->request = NULL;
	}

	return res;
}

static void ata_dev_probe(device_t *device, ata_dev_t *dev,
		blk_req_queue_t **req_queue)
{
	ata_chan_t *chan = dev->chan;
	uint8_t val, val1, val2, cmd;
	blkcnt_t blkcnt;
	blk_dev_t *bdev;
	uint16_t *buf;
	uint8_t pblk;
	int err;

	val1 = ata_readb(chan, ATA_COUNT);
	val2 = ata_readb(chan, ATA_SECT);
	if(val1 != 0x1 || val2 != 0x1) {
		return;
	}

	val1 = ata_readb(chan, ATA_CYL_LO);
	val2 = ata_readb(chan, ATA_CYL_HI);
	val = ata_readb(chan, ATA_STA);

	if(val1 == 0x14 && val2 == 0xeb) {
		dev->flags |= ATA_DEV_ATAPI;
		//timeout = ATAPI_WAIT_ID;
		cmd = ATAPI_CMD_ID;
		kprintf("TODO atapi\n");
		return;
	} else if(val1 == 0x00 && val2 == 0x00 && val) {
		//timeout = ATA_WAIT_WORSTCASE;
		cmd = ATA_CMD_ID;
	} else {
		/* TODO SATA, SATAPI */
		if(val1 != 0xFF && val2 != 0xFF) {
			device_printf(device, "unknown device type: 0x%d "
				"0x%x\n", val1, val2);
		}

		/*
		 * No device here.
		 */
		return;
	}

	buf = kmalloc(ATA_SECT_SZ, VM_WAIT);

	/*
	 * Prepare identify command.
	 */
	chan->tf.in_buf = (void *) buf;
	chan->tf.in_sz = ATA_SECT_SZ;

	/*
	 * Launch identify command.
	 */
	err = ata_cmd(chan, cmd);
	if(err) {
		kfree(buf);
		device_printf(device, "identify failed\n");
		return;
	}

	/*
	 * Check if the device supports DMA.
	 */
	if(chan->prdt && F_ISSET(buf[ATA_ID_CAPS], ATA_CAPS_DMA)) {
		dev->flags |= ATA_DEV_DMA;
	}

	/*
	 * Get the number of logical sectors from the device.
	 * TODO don't do this for ATAPI
	 */
	if(buf[ATA_ID_SUPCF] & ATA_SUPCF_LBA48) {
		dev->flags |= ATA_DEV_LBA48;
		dev->io_max_sect = UINT16_MAX + 1;

		blkcnt = buf[ATA_ID_SECT48];
		blkcnt |= (uint64_t) buf[ATA_ID_SECT48 + 1] << 16;
		blkcnt |= (uint64_t) buf[ATA_ID_SECT48 + 2] << 32;
		blkcnt |= (uint64_t) buf[ATA_ID_SECT48 + 3] << 48;
	} else if(buf[ATA_ID_CAPS] & ATA_CAPS_LBA) {
		dev->io_max_sect = UINT8_MAX + 1;
		blkcnt = buf[ATA_ID_SECT28];
		blkcnt = buf[ATA_ID_SECT28] << 16;
	} else {
		device_printf(device, "device too old: does not support LBA\n");
		kfree(buf);
		return;
	}

	pblk = buf[ATA_ID_SECTSZ] & 0xF;

#if 0
	const char *model = ata_fix_str(buf + ATA_ID_MODEL, ATA_MODEL_LEN);
	const char *serial = ata_fix_str(buf + ATA_ID_SERIAL, ATA_SERIAL_LEN);
	kprintf("%s %s\n", model, serial);
#endif

	kfree(buf);

	/*
	 * Both, the master and the slave, share the same request list, which
	 * only allows 1 request at any time on the channel (i.e. master and
	 * slave cannot handle a request simultaniously).
	 */
	if(*req_queue == NULL) {
		*req_queue = blk_req_queue_alloc(1);
	}

	/*
	 * Allocate a new block device structure.
	 */
	bdev = blk_dev_alloc();
	bdev->start = ata_start_req;
	bdev->req_queue = *req_queue;
	bdev->priv = dev;
	bdev->blk_shift = ATA_SECT_LG; /* 512 byte logical sectors */
	bdev->pblk_shift = ATA_SECT_LG + pblk; /* physical sector size */
	bdev->blkcnt = blkcnt;
	dev->blkdev = bdev;
	bdev->flags = 0; /* TODO atapi */
	if(!F_ISSET(dev->flags, ATA_DEV_DMA)) {
		bdev->flags |= BLK_DEV_MAPIO;
	}

	return;
}

static int ata_intr(void *arg, __unused int intr) {
	ata_chan_t *chan = arg;

	/*
	 * Check if the device really issued an interrupt. This
	 * also clears interrupt status.
	 */
	chan->intr_sta = ata_readb(chan, ATA_STA);
	if(!chan->intr_sta) {
		return BUS_INTR_STRAY;
	} else {
		/*
		 * Schedule the interrupt handling thread.
		 */
		return BUS_INTR_ITHR;
	}
}

static void ata_intr_thr(void *arg) {
	ata_chan_t *chan = arg;
	blk_req_t *req = chan->request;
	int err = 0;

	if(chan->intr_sta & ATA_S_ERR) {
		err = -EIO;
		goto out;
	}

	if(chan->tf.dma) {
		/*
		 * Make sure that the data is visible to us.
		 */
		if(chan->tf.dma_dir == ATA_DMA_TO_CPU) {
			bus_dma_sync(&chan->dma, BUS_DMA_SYNC_CPU);
		}
		ata_finish_dma(chan);

		if(chan->tf.dma_err) {
			err = -EIO;
		}
	} else if(chan->tf.in_buf) {
		if(!(chan->intr_sta & ATA_S_DRQ)) {
			err = -EIO;
		} else for(size_t i = 0; i < (chan->tf.in_sz >> 2); i++) {
			/*
			 * Assume that the device supports dword IO.
			 */
			chan->tf.in_buf[i] = bus_res_readl(&chan->io, ATA_DATA);
		}
	}

out:
	/*
	 * Reset task file.
	 */
	chan->tf.dma = 0;
	chan->tf.dma_err = 0;
	chan->tf.out_buf = NULL;
	chan->tf.in_buf = NULL;

	if(err == 0) {
		chan->tf.success = 1;
	}

	if(req) {
		chan->request = NULL;

		/*
		 * Finished this request and allow new requests.
		 */
		blk_dev_req_done(req, err);
	} else if(chan->tf.cmd == ATA_CMD_ID || chan->tf.cmd == ATAPI_CMD_ID) {
		wakeup(&chan->intrwq, SCHED_IO);
	}

	return;
}

static int ata_ch_probe(device_t *device) {
	if(!ata_channel(device)) {
		return DEVICE_PROBE_FAIL;
	} else {
		return DEVICE_PROBE_OK;
	}
}

static int ata_ch_attach(device_t *device) {
	ata_chan_t *chan = ata_channel(device);
	size_t ndev = 0;
	int err;

	err = bus_alloc_res_name(device, BUS_RES_IO, 0, "io", RF_MAP,
		&chan->io);
	if(err) {
		return err;
	}

	/*
	 * Poke some regs to check if anything is present.
	 */
	ata_writeb(chan, ATA_COUNT, 0x55);
	ata_writeb(chan, ATA_SECT, 0xAA);
	if(ata_readb(chan, ATA_COUNT) != 0x55 ||
		ata_readb(chan, ATA_SECT) != 0xAA)
	{
		device_printf(device, "channel not present\n");
		err = -ENODEV;
		goto err_nodev1;
	}

	err = bus_alloc_res_name(device, BUS_RES_IO, 0, "cntl", RF_MAP,
		&chan->cntl);
	if(err) {
		goto err_cntl;
	}

	/*
	 * The channel seems to be working -> allocate the interrupt.
	 */
	err = bus_alloc_res_name(device, BUS_RES_INTR, 0, "intr", RF_NONE,
		&chan->intr);
	if(err) {
		goto err_intr;
	}

	err = bus_setup_intr(device, ata_intr, ata_intr_thr, chan, &chan->intr);
	if(err) {
		goto err_setup_intr;
	}

	/*
	 * Issue a soft reset.
	 */
	ata_cntl(chan, ATA_C_NIEN | ATA_C_SRST);
	ata_delay(chan);
	ata_cntl(chan, 0); /* This enables interrupts */

	/*
	 * Wait for the BSY bit to clear.
	 */
	if(!ata_bsy_wait(chan, 2000)) {
		device_printf(device, "error: timeout during reset\n");
		err = -ENODEV;
		goto err_nodev2;
	}

	/*
	 * Initialize the channel structure.
	 */
	waitqueue_init(&chan->intrwq);
	chan->tf.out_buf = NULL;
	chan->tf.in_buf = NULL;
	chan->tf.dma = 0;
	chan->prdt = NULL;
	chan->request = NULL;

	/*
	 * Setup dma if supported.
	 */
	if(F_ISSET(chan->cntlr->flags, ATA_CNTLR_DMA)) {
		/*
		 * Allocate memory for the prdts. The buffer was initialized
		 * by the parent bus (e.g. ata-pci)
		 */
		err = bus_dma_alloc_mem(&chan->prdt_dma, ATA_PRDT_MEM);
		if(err == 0) {
			chan->prdt = bus_dma_get_map(&chan->prdt_dma);
			chan->prdt_addr = bus_dma_addr(&chan->prdt_dma);
			bus_dma_create_buf(device,
					0x0, /* start */
					BUS_MAX32, /* end */
					0x2, /* alignment */
					64 << KB_SHIFT, /* boundary */
					ATA_PRDT_NUM, /* nseg */
					64 << KB_SHIFT, /* segsz_max */
					0,
					&chan->dma
				);
		}
	}

	chan->master.chan = chan;
	chan->master.drive = 0;
	chan->master.flags = 0;
	chan->master.blkdev = NULL;
	chan->slave.chan = chan;
	chan->slave.drive = ATA_D_SLAVE;
	chan->slave.flags = 0;
	chan->slave.blkdev = NULL;
	chan->req_queue = NULL;

	/*
	 * Probe the master/slave device.
	 */
	ata_dev_probe(device, &chan->master, &chan->req_queue);
	if(ata_select(&chan->slave)) {
		ata_dev_probe(device, &chan->slave, &chan->req_queue);
	}

	/*
	 * Register the block devices.
	 */
	if(chan->master.blkdev) {
		blk_dev_register(chan->master.blkdev);
		ndev++;
	}
	if(chan->slave.blkdev) {
		blk_dev_register(chan->slave.blkdev);
		ndev++;
	}

	/*
	 * No devices found => channel is not needed.
	 */
	if(ndev == 0) {
		err = -ENODEV;
		goto err_nodev2;
	}

	device_set_desc(device, "ATA channel");

	return 0;

err_nodev2:
err_setup_intr:
	bus_free_res(device, &chan->intr);
err_intr:
	bus_free_res(device, &chan->cntl);
err_cntl:
err_nodev1:
	bus_free_res(device, &chan->io);
	return err;
}

static int ata_ch_detach(device_t *device) {
	ata_chan_t *chan = ata_channel(device);

	if(chan->master.blkdev) {
		blk_dev_unregister(chan->master.blkdev);
	}
	if(chan->slave.blkdev) {
		blk_dev_unregister(chan->slave.blkdev);
	}
	if(chan->req_queue) {
		blk_req_queue_free(chan->req_queue);
	}

	bus_free_res(device, &chan->intr);
	bus_free_res(device, &chan->io);
	bus_free_res(device, &chan->cntl);

	/*
	 * Free memory for the prdts, the buffer is destroyed
	 * by the parent device.
	 */
	bus_dma_free_mem(&chan->prdt_dma);
	bus_dma_destroy_buf(&chan->dma);

	waitqueue_destroy(&chan->intrwq);

	return 0;
}

static DRIVER(ata_ch_driver) {
	.name = "ata-channel",
	.flags = DEV_DEP_INTR,

	/*
	 * The private pointer is provided by parent as bus_priv pointer.
	 */
	.private = 0,
	.type = DEVICE_DISK,
	.probe = ata_ch_probe,
	.attach = ata_ch_attach,
	.detach = ata_ch_detach,
};

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
#include <device/intr.h>

#define PIC_IRQ_SLAVE	0x02 /* Specs say this has to be 2 */
#define PIC_IRQNUM_ALL	0x10 /* 16 interrupts */
#define PIC_IRQNUM	0x08 /* The number of interrupts of the
			      * individual controller (master / slave)
			      */
#define REG_ICW1	0x0
#define REG_OCW2	0x0
#define REG_OCW3	0x0
#define 	ICW1 		(ICW1_INIT | ICW1_IC4)
#define 	ICW1_IC4	(1 << 0)
#define 	ICW1_SNGL	(1 << 1) /* Must be programmed to 0 */
#define 	ICW1_ADI	(1 << 2) /* ignored */
#define 	ICW1_LTIM	(1 << 3) /* replaced by ELCR */
#define 	ICW1_INIT	(1 << 4)
#define		OCW2_EOI	(1 << 5)
#define		OCW2_SEL	(0 << 3)
#define		OCW3_IRR	(0x2)
#define		OCW3_ISR	(0x3)
#define 	OCW3_SEL	(1 << 3)
#define		OCW3_ESMM	(1 << 5)
#define		OCW3_SMM	(1 << 6)
#define REG_ICW2	0x1
#define REG_ICW3	0x1
#define REG_ICW4	0x1
#define REG_OCW1	0x1
#define 	ICW4 		(ICW4_8086)
#define 	ICW4_8086	(1 << 0)
#define 	ICW4_AEOI	(1 << 1)
#define 	ICW4_BUFFER	(1 << 2) /* not used */
#define 	ICW4_BUF 	(1 << 3) /* must be zero */
#define 	ICW4_SFNM	(1 << 4) /* normally disabled */

#define ELCR1		0x0
#define ELCR2		0x1
#define		ELCR_LVL(i)	(1 << (i))

#define i8259a_mwrite(pic, off, val) bus_res_writeb(&(pic)->io_master, off, val) 
#define i8259a_mread(pic, off) bus_res_readb(&(pic)->io_master, off)
#define i8259a_swrite(pic, off, val) bus_res_writeb(&(pic)->io_slave, off, val)

#define PIC(intcntlr) container_of(intcntlr, i8259a_pic_t, cntlr)
typedef struct i8259a_pic {
	bus_res_t cpu_irqs; /* IRQs allocated from CPU */
	bus_res_t io_slave;
	bus_res_t io_master;
	bus_res_t elcr;

	intr_cntlr_t cntlr;
	intr_src_t intrs[PIC_IRQNUM_ALL];

	uint16_t mask;
	uint8_t vec_off;
} i8259a_pic_t;

/**
 * For PIIX: 
 * IRQ0, 1, 2, 8#, and 13 can not be programmed for level sensitive mode
 *   and can not be modified by software.
 */

static void i8259a_setmask(i8259a_pic_t *pic) {
	i8259a_mwrite(pic, REG_OCW1, pic->mask);
	i8259a_swrite(pic, REG_OCW1, pic->mask >> 8);
}

static void i8259a_unmask(i8259a_pic_t *pic, int intr) {
	pic->mask &= ~(1 << intr);
	i8259a_setmask(pic);
}

static void i8259a_mask(i8259a_pic_t *pic, int intr) {
	pic->mask |= (1 << intr);
	i8259a_setmask(pic);
}

static bool i8259a_config(intr_cntlr_t *cntlr, intnum_t num, int flags) {
	const int mask = BUS_INTR_TRIG_MASK | BUS_INTR_POL_MASK;
	i8259a_pic_t *pic = PIC(cntlr);
	intr_src_t *src;

	/*
	 * Don't allow reconfiguration of interrupts here. It would technically
	 * be possible, but...
	 * For PIIX: 
	 * IRQ0, 1, 2, 8#, and 13 can not be programmed for level sensitive mode
	 * and can not be modified by software.
	 */
	src = intr_src(cntlr, num);
	if((flags & mask) != (src->flags & mask)) {
		return false;
	}

	if(flags & BUS_INTR_MASKED) {
		i8259a_mask(pic, num);
	} else {
		i8259a_unmask(pic, num);
	}

	return true;
}

static int i8259a_intr_handle(void *arg, int num) {
	i8259a_pic_t *pic = arg;
	uint8_t irq;
	int res;

	irq = num - pic->vec_off;

	/*
	 * When the IRQ input goes inactive before the acknowledge cycle
	 * starts, a default IRQ7 occurs when the CPU acknowledges the
	 * interrupt.
	 */
	if(irq == 7) {
		uint8_t mask;

		i8259a_mwrite(pic, REG_OCW3, OCW3_SEL | OCW3_ISR);
		mask = i8259a_mread(pic, REG_OCW3);

		/*
		 * A normal IRQ7 sets this bit.
		 * TODO: "However, If a default IRQ7 routine occurs during
		 * a normal IRQ7 routine, the ISR remains set"
		 */
		if(!(mask & (1 << 7))) {
			return BUS_INTR_STRAY;
		}
	}

	res = intr_handle(&pic->cntlr, irq);
	
	/*
	 * Send EOI.
	 */
	if(irq >= 8) {
		i8259a_swrite(pic, REG_OCW2, OCW2_EOI);
	}
	i8259a_mwrite(pic, REG_OCW2, OCW2_EOI);
	
	return res;
}

static int i8259a_setup_res(device_t *device, bus_res_t *res) {
	i8259a_pic_t *pic = device_priv(device);
	intr_add_handler(&pic->cntlr, res);
	return 0;
}

static void i8259a_teardown_res(device_t *device, bus_res_t *res) {
	i8259a_pic_t *pic = device_priv(device);
	intr_remove_handler(&pic->cntlr, res);
}

static int i8259a_alloc_res(device_t *device, bus_res_req_t *req) {
	i8259a_pic_t *pic = device_priv(device);
	int err;

	err = intr_alloc(&pic->cntlr, req);
	if(!err) {
		device_add_ref(device, bus_res_get_device(req->res));	
	}

	return err;
}

static void i8259a_free_res(device_t *device, bus_res_t *res) {
	i8259a_pic_t *pic = device_priv(device);
	intr_free(&pic->cntlr, res);
	device_remove_ref(device, bus_res_get_device(res));
}

static int i8259a_probe(__unused device_t *device) {
	return DEVICE_PROBE_OK;
}

static int i8259a_attach(device_t *device) {
	i8259a_pic_t *pic = device_priv(device);
	uint16_t elcr;
	int err;

	/*
	 * Allocate 16 interrupts from the parent!
	 * Alignment is 0x8 here since the lowest three
	 * bits of ICW2 (Interrupt Vector Base Address)
	 * are ignored!
	 */
	err = bus_alloc_res_at(device, BUS_RES_INTR, 0,
		0, /* start */
		0xFF, /* end */
		16, /* size */
		0x8, /* align*/
		RF_NONE, /* flags */
		&pic->cpu_irqs);
	if(err) {
		goto err_intr;
	}

	err = bus_alloc_res_name(device, BUS_RES_IO, 0, "io-master",
			RF_MAP, &pic->io_master);
	if(err) {
		goto err_master;
	}

	err = bus_alloc_res_name(device, BUS_RES_IO, 0, "io-slave",
			RF_MAP, &pic->io_slave);
	if(err) {
		goto err_slave;
	}

	err = bus_alloc_res_name(device, BUS_RES_IO, 0, "io-elcr",
			RF_MAP, &pic->elcr);
	if(err) {
		goto err_elcr;
	}

	pic->vec_off = bus_res_get_addr(&pic->cpu_irqs);

	/*
	 * Setup the interrupt allocator for this interrupt cntlr.
	 */
	pic->cntlr.config = i8259a_config;
	intr_cntlr_init(&pic->cntlr, pic->intrs, PIC_IRQNUM_ALL);
	intr_reserve(&pic->cntlr, PIC_IRQ_SLAVE);

	elcr = bus_res_readb(&pic->elcr, ELCR1);
	elcr |= bus_res_readb(&pic->elcr, ELCR2) << 8;

	device_printf(device, "IRQ configuration:\n");

	/*
	 * Read ELCR configuration.
	 */
	for(size_t i = 0; i < PIC_IRQNUM_ALL; i++) {
		int flags = BUS_POL_HI;
		const char *trig;

		if(elcr & ELCR_LVL(i)) {
			flags |= BUS_TRIG_LVL | BUS_INTR_SHARED;
			trig = "level";
		} else {
			flags |= BUS_TRIG_EDGE;
			trig = "edge";
		}

		intr_config(&pic->cntlr, i, flags);
		kprintf("\tIRQ%d: %s triggered, active high\n", i, trig);
	}

	/*
	 * Setup the master and slave PIC.
	 */
	i8259a_mwrite(pic, REG_ICW1, ICW1);
	i8259a_swrite(pic, REG_ICW1, ICW1);
	
	/*
	 * ICW2 - Interrupt Vector Base Address
	 */
	i8259a_mwrite(pic, REG_ICW2, pic->vec_off);
	i8259a_swrite(pic, REG_ICW2, pic->vec_off + PIC_IRQNUM);
	
	/*
	 * ICW3
	 */
	i8259a_mwrite(pic, REG_ICW3, 1 << PIC_IRQ_SLAVE);
	i8259a_swrite(pic, REG_ICW3, PIC_IRQ_SLAVE);
	
	/*
	 * ICW4
	 */
	i8259a_mwrite(pic, REG_ICW4, ICW4);
	i8259a_swrite(pic, REG_ICW4, ICW4);

	/*
	 * Mask every interrupt (except cascade of course). This is very
	 * convenient, because if the chipset drivers choose to prefer
	 * the APIC interrupt controllers, this driver can be attached
	 * and everything is masked as it should be.
	 */
	pic->mask = 0xFFFF & ~(1 << PIC_IRQ_SLAVE);
	i8259a_setmask(pic);

	/*
	 * Set interrupt handler.
	 */
	err = bus_setup_intr(device, i8259a_intr_handle, NULL, pic,
		&pic->cpu_irqs);
	if(err) {
		goto err_setup_intr;
	}

	device_set_desc(device, "8259A PIC");

	return 0;

err_setup_intr:
	bus_free_res(device, &pic->elcr);
err_elcr:
	bus_free_res(device, &pic->io_slave);
err_slave:
	bus_free_res(device, &pic->io_master);
err_master:
	bus_free_res(device, &pic->cpu_irqs);
err_intr:
	return err;
}

static int i8259a_detach(device_t *device) {
	i8259a_pic_t *pic = device_priv(device);

	/*
	 * Mask every interrupt.
	 */
	pic->mask = 0xFFFF;
	i8259a_setmask(pic);

	intr_cntlr_destroy(&pic->cntlr);

	/*
	 * Give up the resources.
	 */
	bus_free_res(device, &pic->cpu_irqs);
	bus_free_res(device, &pic->io_master);
	bus_free_res(device, &pic->io_slave);

	return 0;
}

static bus_driver_t i8259a_bus_driver = {
	.bus_alloc_res = i8259a_alloc_res,
	.bus_free_res = i8259a_free_res,
	.bus_setup_res = i8259a_setup_res,
	.bus_teardown_res = i8259a_teardown_res,
};

static DRIVER(i8259a_driver) {
	.name = "i8259a",
	.flags = DEV_DEP_INTR,
	.type = DEVICE_INTR_CNTLR,
	.private = sizeof(i8259a_pic_t),
	.probe = i8259a_probe,
	.attach = i8259a_attach,
	.detach = i8259a_detach,
	.bus = &i8259a_bus_driver,
};

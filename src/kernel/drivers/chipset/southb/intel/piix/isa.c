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

/* PCI config registers */

/* Registers for all PIIX versions */
#define IORT	0x4C /* 1b, ISA I/O RECOVERY TIMER */
#define XBCS	0x4E /* 1b (PIIX), 2 bytes (PIIX3), X-BUS CHIP SELECT */
#define 	XBCS_RTC 	(1 << 0) /* RTC Address Location Enable (70–77h) */
#define 	XBCS_KBD	(1 << 1) /* KBD Cntlr Address Enable (60h and 64h) */
#define 	XBCS_BIOSCS	(1 << 2) /* BIOSCS# Write Protect Enable */
#define 	XBCS_ALIAS	(1 << 3) /* PIIX4, Port 61h Alias Enable */
#define 	XBCS_MOU	(1 << 4) /* IRQ12/M Mouse Function Enable */
#define 	XBCS_COPROC	(1 << 5) /* Coprocessor Error function Enable (IRQ13) */
#define 	XBCS_LOBIOS	(1 << 6) /* Lower BIOS Enable */
#define 	XBCS_EBIOS	(1 << 7) /* Extended BIOS Enable */
#define 	XBCS_APIC	(1 << 8) /* PIC Chip Select  */
#define 	XBCS_1MBIOS	(1 << 9) /* PIIX4, 1-Meg Extended BIOS Enable */
#define 	XBCS_MICRO	(1 << 10) /* PIIX4, Micro Controller Address Enable
									(62h,66h) */
#define PIRQRC	0x60 /* 4b, PIRQx ROUTE CONTROL REGISTERS */
#define PIRQRC_NUM 4
#define 	PIRQRC_DISABLED	(1 << 7)
#define 	PIRQRC_IRQ	(0x0F) /* irq mask */
#define TOM	0x69 /* 1b, TOP OF MEMORY */
#define MSTAT 	0x6A /* 2b, MISCELLANEOUS STATUS */
/* MOTHERBOARD DEVICE DMA CONTROL REGISTERS */
#define MBDMA0	0x76 /* 1b */
#define MBDMA1	0x77 /* 1b */

/* PIIX registers */
#define MBIRQ1	0x71 /* 1b, MOTHERBOARD DEVICE IRQ ROUTE CONTROL */

/* PIIX/PIIX3 registers */
#define MBIRQ0	0x70 /* 1b, MOTHERBOARD DEVICE IRQ ROUTE CONTROL */
#define PCSC 	0x78 /* 2b, PROGRAMMABLE CHIP SELECT CONTROL */
#define SMICNTL	0xA0 /* 1b, SMI CONTROL */
#define SMIEN	0xA2 /* 2b, SMI ENABLE */
#define SEE 	0xA4 /* 4b, SYSTEM EVENT ENABLE */
#define FTMR	0xA8 /* 1b, FAST OFF TIMER */
#define SMIREQ	0xAA /* 2b, SMI REQUEST */
#define CTLTMR 	0xAC /* 1b, CLOCK SCALE STPCLK# LOW TIMER */
#define CTHTMR	0xAE /* 1b, CLOCK SCALE STPCLK# HIGH TIMER */

/* PIIX3 registers */

/* PIIX3/PIIX4 registers */
#define APICBASE	0x80 /* 1b, APIC BASE ADDRESS RELOCATION REGISTER */
#define DLC		0x82 /* 1b, DETERMINISTIC LATENCY CONTROL REGISTER */

/* PIIX4 registers */
#define SERIRQC	0x64
#define PDMACFG	0x90 /* 2b */
#define DDMABP0	0x92 /* 2b */
#define DDMABP1	0x94 /* 2b */
#define GENCFG	0xB0 /* 4b */
#define RTCCFG	0xCB /* 1b */

/* Memory */
#define BIOS_ALIAS_OFF	0xFFF00000

#define BIOS_LO_ADDR	0xE0000
#define BIOS_HI_ADDR	0xF0000
#define BIOS_END	0x100000
#define EXTBIOS1M_ADDR	0xFFF00000
#define EXTBIOS_ADDR	0xFFF80000

#define PIIX_IO_RC 	0xcf9
/*
 * Reset X-Bus IRQ12 And IRQ1 Register
 * NOT ALLOCATED SINCE THIS IS ps2-cntlr IO
 */
#define PIIX_IO_XBR	0x060
#define PIIX_IO_APM	0x0B2
#define PIIX_IO_A20	0x092

typedef struct piix_isab {
#define PIIX 	0
#define PIIX3 	1
#define PIIX4 	2
	int version;

	bus_res_t bios_lo;
	bus_res_t bios_hi;

	bus_res_t rc_io; /* Reset Control Register */
	bus_res_t apm_io;
	bus_res_t a20_io; /* Fast A20 Gate */

	uint16_t xbcs;
} piix_isab_t;

static int piix_isa_mthd(device_t *device, int cmd, void *arg) {
	pci_pirq_route_t *pirq = arg;
	uint8_t route;

	if(cmd != DEVICE_PCI_PIRQ_ROUTE || pirq->pirq >= PIRQRC_NUM) {
		return -EINVAL;
	} else if(pirq->slot == 0x01) {
		/*
		 * The power management device lies on slot 1 and uses SCI
		 * interrupt which can only be 9 in this case.
		 *
		 * TODO
		 * SeaBIOS does not set IRQ9 to be level triggered in the ELCR,
		 * but isn't this a PCI interrupt?
		 * => EDGE | ACT_HI ?????
		 */
		pirq->irq = 0x9;
	}

	route = pci_cfg_readb(device, PIRQRC + pirq->pirq);
	if(F_ISSET(route, PIRQRC_DISABLED)) {
		kpanic("route disabled: %d\n", pci_cfg_readb(device, PIRQRC +
			pirq->pirq));
		return -ENXIO;
	}

	device_printf(device, "routed slot 0x%x + PIRQ%c -> IRQ%d\n",
		pirq->slot, 'A' + pirq->pirq, pirq->irq);
	pirq->irq = route & PIRQRC_IRQ;

	return 0;

}

static int piix_isa_probe(__unused device_t *device) {
	return DEVICE_PROBE_OK;
}

static int piix_alloc_resources(device_t *device, piix_isab_t *isab) {
	uint32_t addr;
	int err;

	/* TODO  512–640-Kbyte main memory region (80000– 9FFFFh) */

	addr = BIOS_HI_ADDR;
	if(isab->xbcs & XBCS_LOBIOS) {
		addr = BIOS_LO_ADDR;
	}

	err = bus_alloc_res_fixed(device, BUS_RES_MEM, 0, addr,
		BIOS_END - addr, RF_NONE, &isab->bios_lo);
	if(err) {
		goto err_lobios;
	}

	/*
	 * The bios in low memory is aliased directly below 4GB.
	 */
	addr += BIOS_ALIAS_OFF;
	if(isab->xbcs & XBCS_EBIOS) {
		addr = EXTBIOS_ADDR;
	}
	if(isab->xbcs & XBCS_1MBIOS) {
		addr = EXTBIOS1M_ADDR;
	}

	err = bus_alloc_res_fixed(device, BUS_RES_MEM, 0, addr,
		(UINT32_MAX - addr) +1, RF_NONE, &isab->bios_hi);
	if(err) {
		goto err_hibios;
	}

	err = bus_alloc_res_fixed(device, BUS_RES_IO, 0, PIIX_IO_RC, 0x1,
			RF_NONE, &isab->rc_io);
	if(err) {
		goto err_rc;
	}

	err = bus_alloc_res_fixed(device, BUS_RES_IO, 0, PIIX_IO_APM, 0x2,
			RF_NONE, &isab->apm_io);
	if(err) {
		goto err_apm;
	}

	err = bus_alloc_res_fixed(device, BUS_RES_IO, 0, PIIX_IO_A20, 0x1,
			RF_NONE, &isab->a20_io);
	if(err) {
		goto err_a20;
	}

	return 0;

err_a20:
	bus_free_res(device, &isab->apm_io);
err_apm:
	bus_free_res(device, &isab->rc_io);
err_rc:
	bus_free_res(device, &isab->bios_hi);
err_hibios:
	bus_free_res(device, &isab->bios_lo);
err_lobios:
	return err;
}

static int piix_isa_attach(device_t *device) {
	piix_isab_t *isab = device_priv(device);
	uint16_t devid;
	const char *vstr;
	int err;

	devid = pcidev_device(device);
	switch(devid) {
		case PCI_ID_PIIX3_ISA:
			isab->version = PIIX3;
			vstr = "3";
			break;
		case PCI_ID_PIIX4_ISA:
			isab->version = PIIX4;
			vstr = "4";
			break;
		default:
			isab->version = PIIX;
			vstr = "";
			break;
	}

	if(isab->version == PIIX) {
		isab->xbcs = pci_cfg_readb(device, XBCS);
	} else {
		isab->xbcs = pci_cfg_readw(device, XBCS);
	}

	err = piix_alloc_resources(device, isab);
	if(err) {
		return err;
	}

	device_set_desc(device, "PIIX%s PCI to ISA bridge", vstr);
	bus_probe_children(device);

	return 0;
}

static int piix_isa_detach(device_t *device) {
	piix_isab_t *isab = device_priv(device);
	int err;

	err = bus_generic_detach(device);
	if(!err) {
		bus_free_res(device, &isab->bios_lo);
		bus_free_res(device, &isab->bios_hi);
		bus_free_res(device, &isab->rc_io);
		bus_free_res(device, &isab->apm_io);
		bus_free_res(device, &isab->a20_io);
	}

	return err;
}

static DEVTAB(piix_isa_tab, "piix-isa", "pci") {
	{ .id = PCI_ID(PCIV_INTEL, PCI_ID_PIIX3_ISA, PCI_CLASS_BRIDGE_ISA) },
	{ .id = PCI_ID(PCIV_INTEL, PCI_ID_PIIX4_ISA, PCI_CLASS_BRIDGE_ISA) },
	{ .id = PCI_ID(PCIV_INTEL, PCI_ID_PIIX_ISA, PCI_CLASS_BRIDGE_ISA) },
	{ DEVTAB_END },
};

static bus_driver_t piix_isa_bus_driver = {
	.bus_child_detach = NULL,
	.bus_alloc_res = bus_generic_alloc_res,
	.bus_free_res = bus_generic_free_res,
	.bus_setup_res = bus_generic_setup_res,
	.bus_teardown_res = bus_generic_teardown_res,
};

static DRIVER(piix_isa_driver) {
	.name = "piix-isa",
	.flags = 0,
	.type = DEVICE_CHIPSET,
	.private = sizeof(piix_isab_t),
	.devtab = &piix_isa_tab,
	.probe = piix_isa_probe,
	.attach = piix_isa_attach,
	.detach = piix_isa_detach,
	.new_pass = bus_generic_new_pass,
	.mthd = piix_isa_mthd,
	.bus = &piix_isa_bus_driver,
};

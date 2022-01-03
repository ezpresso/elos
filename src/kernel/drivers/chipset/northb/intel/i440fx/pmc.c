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

#define CONFADD		0x0CF8
#define 	CONE 		(1U << 31)
#define 	BUSNUM_SHIFT	16
#define 	DEVNUM_SHIFT	11
#define 	FUNCNUM_SHIFT	8
#define 	REGNUM_SHIFT	2
#define CONFDATA	0x0CFC

/* 82441FX PCI AND MEMORY CONTROLLER registers */
#define PMCCFG		0x50 /* 2b */
#define DETURBO		0x52 /* 1b */
#define DBC		0x53 /* 1b */
#define AXC		0x54 /* 1b */
#define DRAMR		0x55 /* 2b */
#define DRAMC 		0x57 /* 1b */
#define DRAMT		0x58 /* 1b */
#define PAM0 		0x59 /* 1b each, 7 registers */
#define		PAM_RE_0	(1 << 0)
#define		PAM_WE_0	(1 << 1)
#define		PAM_RE_1	(1 << 4)
#define		PAM_WE_1	(1 << 5)
#define 	PAM_NUM		7
/* QEMU bug: DRB7 is 0x57 instead of 0x67 */
#define DRB0 		0x60 /* 1b each, 8 registers */
#define 	DRB_NUM		8
#define FDHC 		0x68 /* 1b */
#define 	FDC_HEN_MASK	MASK(8, 7, 6)
#define 		FDC_HEN_NONE	(0 << 6) 
#define 		FDC_HEN_512KB	(1 << 6) /* 512 KB−640 KB hole */
#define 		FDC_HEN_15M	(2 << 6) /* 15 MB−16 MB hole */
#define MTT		0x70 /* 1b */
#define CLT 		0x71 /* 1b */
#define SMRAM		0x72 /* 1b */
#define ERRCMD		0x90 /* 1b */
#define ERRSTS		0x91 /* 1b */
#define TRC		0x93 /* 1b */

#define MEM_START	(1 << MB_SHIFT)
#define HOLE_512_ADDR	(512 << KB_SHIFT)
#define HOLE_512_END	((640 << KB_SHIFT) - 1)
#define HOLE_15M_ADDR	(15 << MB_SHIFT)
#define HOLE_15M_END	((16 << MB_SHIFT) - 1)

typedef struct i440fx_pmc {
	bus_res_t confadd;
	bus_res_t confdata;
} i440fx_pmc_t;

typedef struct pam_region {
	uint32_t start;
	uint32_t end;
} pam_region_t;

#if 0

static const pam_region_t pam_ranges[] = {
	{ 0x0, 0x0 },
	{ 0xF0000, 0xFFFFF },
	{ 0xC0000, 0xC3FFF },
	{ 0xC4000, 0xC7FFF },
	{ 0xC8000, 0xCBFFF },
	{ 0xCC000, 0xCFFFF },
	{ 0xD0000, 0xD3FFF },
	{ 0xD4000, 0xD7FFF },
	{ 0xD8000, 0xDBFFF },
	{ 0xDC000, 0xDFFFF },
	{ 0xE0000, 0xE3FFF },
	{ 0xE4000, 0xE7FFF },
	{ 0xE8000, 0xEBFFF },
	{ 0xEC000, 0xEFFFF },
};

static uint8_t pmc_pci_readb(bus_res_t *res, bus_off_t addr) {
	i440fx_pmc_t *pmc = device_priv(res->map.map_dev);

	sync_scope_acquire(&pmc->pci_lock);
	bus_res_writeb(&pmc->confadd, addr | CONE);
	
	return bus_res_readb(&pmc->confdata);
}

/* static bus_res_acc_t pcicfg_acc = { }; */
#endif

static void pmc_detect_memory(device_t *device) {
	(void) device;

#if 0
	uint64_t dos_end;
	uint8_t fdhc;
	uint8_t pam;
	size_t i;

	fdhc = arch_pci_read(0, 0, FDHC, 1) & FDC_HEN_MASK;
	if(fdhc == FDC_HEN_512KB) {
		mach_conf_mem(HOLE_512_ADDR, HOLE_512_END, MACH_DEV, 0);
		dos_end = HOLE_512_ADDR - 1;
	} else {
		dos_end = HOLE_512_END;
	}

	if(fdhc == FDC_HEN_15M) {
		mach_conf_mem(HOLE_15M_ADDR, HOLE_15M_END, MACH_DEV, 0);	
	}

	/* Reserve DOS-area */
	mach_conf_mem(0, dos_end, MACH_FW, 0);

	/* 640−768-Kbyte Video Buffer Area */
	mach_conf_mem(0xa0000, 0xbffff, MACH_DEV, 0);

	/* 768−896-Kbyte - Expansion Area
	 * 896−960-Kbyte - Extended System BIOS Area
	 * 960-Kbyte−1-Mbyte Memory - System BIOS Area
	 */
	for(i = 0; i < NELEM(pam_ranges); i++) {
		mach_mem_type_t type;
		int re, we, flags = 0;

		if(pam_ranges[i].end == 0) {
			continue;
		}

		pam = arch_pci_read(0, 0, PAM0 + (i / 2), 1);

		if((i & 1) == 0) {
			re = pam & PAM_RE_0;
			we = pam & PAM_WE_0;
		} else {
			re = pam & PAM_RE_1;
			we = pam & PAM_WE_1;
		}

		if(!re && !we) {
			type = MACH_DEV;
		} else {
			if(!re) {
				flags = MACH_MEM_RO;
			}

			type = MACH_FW;
		}
	
		device_printf(device, "0x%x - 0x%x -> %s%s\n",
				pam_ranges[i].start, pam_ranges[i].end,
				mach_mem_type_str(type),
				flags & MACH_MEM_RO ? " (read-only)" : "");
		mach_conf_mem(pam_ranges[i].start, pam_ranges[i].end, type, flags);
	}

	/* Register ram only if mainboard driver has not registered yet */
	if(mach_mem_lookup(MEM_START) == NULL) {
		uint32_t tom = arch_pci_read(0, 0, DRB0 + 7, 1) * (8 << MB_SHIFT);
		mach_add_mem(MEM_START, tom, MACH_RAM);
	} else {
		/* The problem is that QEMU supports more low-memory than the real
		 * hardware does. (DRB7 is 8 bits wide => 255*8BM =2GB)! So the
		 * mainboard driver has to provide that info! On top of that QEMU has
		 * messed up the PCI conf offset of DRB7!
		 */
		kassert(!(arch_pci_read(0, 0, PCI_DEVCFG_SUBSYS_VENDOR, 2) ==
			PCIV_REDHAT && arch_pci_read(0, 0, PCI_DEVCFG_SUBSYS_ID, 2) ==
			PCI_SUBSYS_QEMU));
	}

	/* I/O APIC units are located beginning at the default address FEC0_0000h.
	 * The first I/O APIC is located at FEC0_0000h. Each I/O APIC unit is
	 * located at FEC0_x000h where x is I/O APIC unit number 0 through F(hex).
	 * This address range is normally mapped to PCI (like all other memory
	 * ranges above the Top of Main Memory).
	 */
	mach_conf_mem(0xfec00000, 0xfec0ffff, MACH_DEV, 0)	

	/* "The address range between the APIC configuration space and the High BIOS
	 * (FEC0_FFFFh to FFE0_0000h)
	 * is always mapped to the PCI"
	 */
	mach_conf_mem(0xfec10000, 0xffdfffff, MACH_DEV, 0)	

	mach_conf_mem(0xffe00000, 0xffffffff, MACH_FW, 0);
#endif
}

static int pmc_probe(__unused device_t *device) {
	return DEVICE_PROBE_OK;
}

static int pmc_attach(device_t *device) {
	i440fx_pmc_t *pmc = device_priv(device);
	int err;

	err = bus_alloc_res_fixed(device, BUS_RES_IO, 0, CONFADD, 1,
			RF_MAP, &pmc->confadd);
	if(err) {
		return err;
	}

	err = bus_alloc_res_fixed(device, BUS_RES_IO, 0, CONFDATA, 1,
			RF_MAP, &pmc->confdata);
	if(err) {
		goto err_data;
	}

	pmc_detect_memory(device);

	device_set_desc(device, "Intel 440FX PCI and memory controller");
	bus_probe_children(device);

	return 0;

err_data:
	bus_free_res(device, &pmc->confadd);
	return err;
}

static int pmc_detach(device_t *device) {
	i440fx_pmc_t *pmc = device_priv(device);
	int err;

	err = bus_generic_detach(device);
	if(!err) {
		bus_free_res(device, &pmc->confadd);
		bus_free_res(device, &pmc->confdata);
	}

	return err;
}

#if 0
static DEVTAB(pmc_devtab, "i440fx-pmc", "pci") {
	/* { id: PCI_ID(PCIV_INTEL, PCI_ID_82441FX, PCI_CLASS_BRIDGE_HOST) }, */
	{ DEVTAB_END },
};
#endif

static bus_driver_t pmc_bus_driver = {
	.bus_alloc_res = bus_generic_alloc_res,
	.bus_free_res = bus_generic_free_res,
	.bus_child_detach = NULL,
	.bus_setup_res = bus_generic_setup_res,
	.bus_teardown_res = bus_generic_teardown_res,
};

static DRIVER(pmc_driver) {
	.name = "i440fx-pmc",
	.flags = 0,
	.type = DEVICE_CHIPSET,
	.private = sizeof(i440fx_pmc_t),
	.probe = pmc_probe,
	.attach = pmc_attach,
	.detach = pmc_detach,
	.new_pass = bus_generic_new_pass,
	.bus = &pmc_bus_driver,
};

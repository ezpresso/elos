/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 *
 * Copyright (c) 2018, Elias Zell
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
#include <device/gpu/gpu.h>
#include <device/gpu/mode.h>
#include <drivers/bus/pci.h>

//https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/bochs/bochs_kms.c
//http://cvs.savannah.nongnu.org/viewvc/*checkout*/vgabios/vgabios/vbe_display_api.txt?revision=1.14

#define BOCHS_BAR_FB	PCI_RES_BAR(0)
#define BOCHS_BAR_REGS	PCI_RES_BAR(2)
#define	BOCHS_VGA_IO	0x0400 /* Standard VGA ports 0x3c0 -> 0x3df */
#define BOCHS_DISPI	0x0500 /* Dispi interface registers */
#define		BOCHS_DISPI_REG(idx)	(BOCHS_DISPI + ((idx) << 1))
#define		BOCHS_DISPI_ID		BOCHS_DISPI_REG(0x0)
#define			VBE_DISPI_ID0		0xB0C0
#define			VBE_DISPI_ID1		0xB0C1
#define			VBE_DISPI_ID2		0xB0C2
#define			VBE_DISPI_ID3		0xB0C3
#define			VBE_DISPI_ID4		0xB0C4
#define		BOCHS_DISPI_XRES	BOCHS_DISPI_REG(0x1)
#define		BOCHS_DISPI_YRES	BOCHS_DISPI_REG(0x2)
#define		BOCHS_DISPI_BPP		BOCHS_DISPI_REG(0x3)
#define		BOCHS_DISPI_ENABLE	BOCHS_DISPI_REG(0x4)
#define		BOCHS_DISPI_VIRT_WIDTH	BOCHS_DISPI_REG(0x6)
#define		BOCHS_DISPI_VIRT_HEIGHT	BOCHS_DISPI_REG(0x7)
#define		BOCHS_DISPI_X_OFFSET	BOCHS_DISPI_REG(0x8)
#define		BOCHS_DISPI_Y_OFFSET	BOCHS_DISPI_REG(0x9)
#define		BOCHS_DISPI_VBOX_VIDEO	BOCHS_DISPI_REG(0xa)
#define		BOCHS_DISPI_FB_BASE_HI	BOCHS_DISPI_REG(0xb)
#define BOCHS_QEXT	0x0600 /* QEMU (2.2+) extended registers */

typedef struct bochs {
	gpu_device_t gpu;
	bus_res_t regs;
	bus_res_t fb;

	gpu_crtc_t crtc;
	gpu_connector_t connector;
	gpu_encoder_t encoder;
} bochs_t;

static gpu_driver_t bochs_gpu_driver;
static gpu_encoder_ops_t bochs_encoder_ops = { };
static gpu_connector_ops_t bochs_connector_ops = { };
static gpu_crtc_ops_t bochs_crtc_ops = { };

static int bochs_init_mode(bochs_t *bochs) {
	gpu_device_t *gpu = &bochs->gpu;

	gpu_crtc_init(gpu, &bochs_crtc_ops,  &bochs->crtc);
	gpu_connector_init(gpu, GPU_CONNECTOR_VIRT, &bochs_connector_ops,
		&bochs->connector);
	gpu_encoder_init(gpu, GPU_ENCODER_DAC, &bochs_encoder_ops,
		&bochs->encoder);
	gpu_encoder_add_crtc(&bochs->encoder, &bochs->crtc);
	gpu_connector_add_encoder(&bochs->connector, &bochs->encoder);

	return 0;
}

static int bochs_probe(device_t *device) {
	(void) device;
	return DEVICE_PROBE_OK;
}

static int bochs_attach(device_t *device) {
	bochs_t *bochs = device_priv(device);
	int err;

	err = bus_alloc_res(device, BUS_RES_MEM, BOCHS_BAR_FB, RF_MAP,
		&bochs->fb);
	if(err) {
		return err;
	}

	/*
	 * Try allocating the MMIO resource of the vga device. This
	 * BAR is only supported qemu 1.3+, so it may not be present.
	 */
	err = bus_alloc_res(device, BUS_RES_MEM, BOCHS_BAR_REGS, RF_MAP,
		&bochs->regs);
	if(err) {
		device_printf(device, "QEMU/Bochs VGA MMIO resource not "
			"present.\n\tPlease upgrade your emulator.\n");
		return err;
	}

	device_printf(device, "Framebuffer size: 0x%x\n",
		bus_res_get_size(&bochs->fb));

	gpu_init(&bochs->gpu, &bochs_gpu_driver, bochs);
	bochs_init_mode(bochs);
	gpu_register(&bochs->gpu);

	return 0;
}

static int bochs_detach(device_t *device) {
	(void) device;
	return 0;
}

static DEVTAB(bochs_devtab, "bochs", "pci") {
	{ .id = PCI_ID(PCIV_QEMU, PCI_ID_QEMU_VGA, PCI_CLASS_DISPLAY_VGA) },
	{ DEVTAB_END }
};

static DRIVER(bochs_driver) {
	.name = "bochs",
	.flags = DEV_DEP_INTR,
	.type = DEVICE_GPU,
	.private = sizeof(bochs_t),
	.devtab = &bochs_devtab,
	.probe = bochs_probe,
	.attach = bochs_attach,
	.detach = bochs_detach,
};

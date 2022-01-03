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
#include <drivers/bus/isa.h>

typedef struct uart_pc {
	bus_res_t io;
	bus_res_t intr;
} uart_pc_t;

/* http://www.sci.muni.cz/docs/pc/serport.txt */

/*
 * Registers.
 */
#define UP_DATA			0x0
#define UP_INTREN		0x1
#define UP_BAUD_LO		0x0
#define UP_BAUD_HI		0x1
#define UP_FIFO			0x2
#define UP_LINE_CTRL		0x03
#define UP_MOD_CTRL		0x04
#define UP_LINE_STA		0x05
#define UP_MOD_STA		0x06
#define UP_SCRATCH		0x07

int uart_pc_probe(__unused device_t *device) {
	return DEVICE_PROBE_OK;
}

int uart_pc_attach(device_t *device) {
	uart_pc_t *uart = device_priv(device);
	int err;

	err = bus_alloc_res_name(device, BUS_RES_IO, 0, "io", RF_NONE,
		&uart->io);
	if(err) {
		return err;
	}

	err = bus_alloc_res_name(device, BUS_RES_INTR, 0, "intr", RF_NONE,
		&uart->intr);
	if(err) {
		goto err_intr;
	}

	device_set_desc(device, "Standard PC COM port");
	return 0;

err_intr:
	bus_free_res(device, &uart->io);
	return err;
}

int uart_pc_detach(device_t *device) {
	uart_pc_t *uart = device_priv(device);

	bus_free_res(device, &uart->io);
	bus_free_res(device, &uart->intr);

	return 0;
}

static DEVTAB(uart_pc_devtab, "uart-pc", "isa") {
	{ .id = ISA_ID(ISA_VEN_PNP, 0x0500) }, 
	{ DEVTAB_END }
};

static DRIVER(uart_pc_driver) {
	.name = "uart-pc",
	.flags = DEV_DEP_INTR,
	.type = DEVICE_UART,
	.private = sizeof(uart_pc_t),
	.devtab = &uart_pc_devtab,
	.probe = uart_pc_probe,
	.attach = uart_pc_attach,
	.detach = uart_pc_detach,
};

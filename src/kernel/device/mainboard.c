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
#include <device/mainboard.h>
#include <drivers/bus/pci.h>

static device_t *mboard_device;
static mboard_driver_t *mboard_driver;

int mboard_pci_irq_route(device_t *bus, uint8_t slot, uint8_t pin,
	struct bus_res_req *req)
{
	if(mboard_driver == NULL) {
		kprintf("[mboard] warning: routing PCI interrupt too early\n");
		return -EAGAIN;
	} else {
		return mboard_driver->pci_irq_route(mboard_device, bus, slot,
			pin, req);
	}
}

bool mboard_present(void) {
	return mboard_device != NULL;
}

void mboard_register(device_t *device, mboard_driver_t *driver) {
	kassert(!mboard_device, "[device] mainboard: multiple mainboard "
		"drivers");

	mboard_device = device;
	mboard_driver = driver;
}

void mboard_unregister(device_t *device) {
	assert(mboard_device == device);
	mboard_device = NULL;
}

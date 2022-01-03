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
#include <device/mainboard.h>

static int acpi_probe(__unused device_t *device) {
	/*
	 * Prefer serious motherboard drivers and not that retarded
	 * ACPI nonsense.
	 */
	return DEVICE_PROBE_LOPRIO;
}

static int acpi_attach(device_t *device) {
	(void) device;

	/*
	 * Parse aml and build a device tree based on that.
	 */
	return 0;
}

static int acpi_detach(device_t *device) {
	int err;

	err = bus_generic_detach(device);
	if(!err) {
		kpanic("shutdown acpi");
	}

	return err;
}

/* static mboard_driver_t acpi_mboard = {
}; */

static bus_driver_t acpi_bus_driver = {
	.bus_child_detach = NULL,
	.bus_alloc_res = bus_generic_alloc_res,
	.bus_free_res = bus_generic_free_res,
	.bus_setup_res = bus_generic_setup_res,
	.bus_teardown_res = bus_generic_teardown_res,
};

static DRIVER(acpi_driver) {
	.name = "acpi-motherboard",
	.devtab = NULL,
	.flags = 0,
	.type = DEVICE_CHIPSET,
	.probe = acpi_probe,
	.attach = acpi_attach,
	.detach = acpi_detach,
	.new_pass = bus_generic_new_pass,
	.bus = &acpi_bus_driver,
};

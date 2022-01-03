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
#include <kern/init.h>
#include <device/device.h>
#include <device/_device.h>

static DEFINE_LIST(devtab_list);

static int driver_device_probe(device_driver_t *driver, device_t *device) {
	int res;

	device->driver = driver;
	res = driver->probe(device);
	device->driver = NULL;

	return res;
}

int device_driver_choose(device_t *device) {
	devtab_entry_t *entry, *loprio_entry = NULL;
	device_driver_t *driver, *loprio_driver = NULL;
	devtab_t *devtab;
	int res;

	/*
	 * Check if parent actually told us a driver.
	 */
	if(device->driver_hint) {
		driver = driver_get(device->driver_hint, DRIVER_DEVICE);
		if(driver) {
			res = driver_device_probe(driver, device);
			if(res == DEVICE_PROBE_FAIL) {
				driver_put(driver);
				return -ENODEV;
			}

			device->driver = driver;
			goto out;
		}
	}

	/*
	 * No ID provided by parent device.
	 */
	if(device->ids == NULL) {
		return -ENOENT;
	}

	/*
	 * Search driver by device table.
	 */
	foreach(devtab, &devtab_list) {
		entry = device_match_devtab(device, devtab);
		if(!entry) {
			continue;
		}

		driver = driver_get(devtab->driver, DRIVER_DEVICE);
		if(!driver) {
			continue;
		}

		device->devtab_entry = entry;
		res = driver_device_probe(driver, device);
		if(res == DEVICE_PROBE_OK) {
			if(loprio_driver) {
				driver_put(loprio_driver);
			}

			device->driver = driver;
			goto out;
		}

		device->devtab_entry = NULL;
		if(res == DEVICE_PROBE_LOPRIO) {
			if(!loprio_driver) {
				loprio_driver = driver;
				loprio_entry = entry;
			}
		}
	}


	if(loprio_driver) {
		device->driver = loprio_driver;
		device->devtab_entry = loprio_entry;
	} else {
		return -ENOENT;
	}

out:
	device->state = DEVICE_PROBE;
	return 0;
}

void __init device_driver_init(void) {
	devtab_t *cur_devtab;

	section_foreach(cur_devtab, DEVTAB_SEC) {
		list_node_init(cur_devtab, &cur_devtab->node);
		list_append(&devtab_list, &cur_devtab->node);
	}
}

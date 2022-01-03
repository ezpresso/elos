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
#include <kern/sync.h>
#include <device/device.h>
#include <device/nvram.h>
#include <lib/string.h>

static DEFINE_LIST(nvram_list);
static sync_t nvram_lock = SYNC_INIT(MUTEX);

nvram_t *nvram_search(const char *name) {
	nvram_t *nvram;

	/*
	 * TOOD That's temporary
	 */
	sync_acquire(&nvram_lock);
	foreach(nvram, &nvram_list) {
		if(!strcmp(nvram->name, name)) {
			/*
			 * Don't drop the nvram_lock. nvram_put will do that.
			 */
			return nvram;
		}
	}

	sync_release(&nvram_lock);
	return NULL;
}

void nvram_put(__unused nvram_t *nvram) {
	sync_release(&nvram_lock);
}

static int nvram_rw(nvram_t *nvram, bool wr, uint64_t off, uint8_t size,
		uint64_t *value)
{
	nvram_driver_t *driver = nvram->driver;
	int err = -ENOTSUP;

	if(driver->nvram_rw) {
		err = driver->nvram_rw(nvram, wr, off, size, value);
	}

	return err;
}

int nvram_read(nvram_t *nvram, uint64_t off, uint8_t size, uint64_t *value) {
	return nvram_rw(nvram, false, off, size, value);
}

int nvram_write(nvram_t *nvram, uint64_t off, uint8_t size, uint64_t value) {
	return nvram_rw(nvram, true, off, size, &value);
}

int nvram_register(nvram_t *nvram, nvram_driver_t *driver, const char *name) {
	sync_assert(&device_lock);

	list_node_init(nvram, &nvram->node);
	nvram->name = name;
	nvram->driver = driver;

	sync_scope_acquire(&nvram_lock);
	list_append(&nvram_list, &nvram->node);
	return 0;
}

void nvram_unregister(nvram_t *nvram) {
	synchronized(&nvram_lock) {
		list_remove(&nvram_list, &nvram->node);
	}

	list_node_destroy(&nvram->node);
}

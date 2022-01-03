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
#include <kern/driver.h>
#include <kern/sync.h>
#include <kern/init.h>
#include <lib/hashtab.h>
#include <lib/string.h>

#define DRIVER_HT_SIZE 1024

typedef struct driver {
	DRIVER_COMMON;
} driver_t;

static sync_t driver_lock = SYNC_INIT(MUTEX);
static hashtab_t driver_ht;

static size_t driver_hash(const char *name, driver_type_t type) {
	return hash_str(name) ^ type;
}

void *driver_get(const char *name, driver_type_t type) {
	driver_entry_t *entry;
	driver_t *driver;
	size_t hash;

	hash = driver_hash(name, type);

	sync_scope_acquire(&driver_lock);
	hashtab_search(entry, hash, &driver_ht) {
		driver = entry->driver;

		if(entry->type == type && !strcmp(driver->name, name)) {
			entry->ref++;
			return driver;
		}
	}

	return NULL;
}

void driver_put(void *_driver) {
	driver_t *driver = _driver;

	sync_scope_acquire(&driver_lock);
	driver->drv_entry->ref--;
}

void driver_add(driver_entry_t *entry) {
	driver_t *driver = entry->driver;

	list_node_init(entry, &entry->node);
	entry->hash = driver_hash(driver->name, entry->type);
	entry->ref = 0;
	driver->drv_entry = entry;

	sync_scope_acquire(&driver_lock);
	hashtab_set(&driver_ht, entry->hash, &entry->node);
}

int driver_remove(driver_entry_t *entry) {
	sync_scope_acquire(&driver_lock);
	if(entry->ref != 0) {
		return -EBUSY;
	} else {
		hashtab_remove(&driver_ht, entry->hash, &entry->node);
		return 0;
	}
}

static __init int driver_init(void) {
	driver_entry_t *entry;

	hashtab_alloc(&driver_ht, DRIVER_HT_SIZE, VM_WAIT);
	section_foreach(entry, DRIVER_SECTION) {
		driver_add(entry);
	}

	return INIT_OK;
}

early_initcall(driver_init);
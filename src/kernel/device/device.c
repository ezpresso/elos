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
#include <kern/cpu.h>
#include <kern/symbol.h>
#include <vm/malloc.h>
#include <lib/string.h>
#include <device/device.h>
#include <device/_device.h>

static inline void devprop_free(device_t *device);

sync_t device_lock = SYNC_INIT(MUTEX);

/*
 * The number of devices which will be attached later.
 */
static size_t devices_not_attached = 0;
static int device_deps;
static bool device_early = true;
static DEFINE_LIST(device_list);
device_t *root_device;

static const char *device_state_name(device_state_t state) {
	switch(state) {
	case DEVICE_REGISTER:
		return "registered";
	case DEVICE_PROBE:
		return "probed";
	case DEVICE_ATTACH:
		return "attached";
	case DEVICE_ATTACH_LATER:
		return "attach later";
	default:
		kpanic("[device] unknown device state: %d", state);
	}
}

device_t *device_alloc(const char *drv_hint) {
	device_t *device;

	device = kmalloc(sizeof(*device), VM_WAIT | VM_ZERO);
	device->state = DEVICE_REGISTER;
	device->driver_hint = drv_hint;
	list_node_init(device, &device->node_child);
	list_node_init(device, &device->node_intr);
	list_node_init(device, &device->node);
	list_init(&device->children);
	list_init(&device->intr_children);
	list_init(&device->proplist);

	list_append(&device_list, &device->node);

	return device;
}

void device_free(device_t *device) {
	assert(device->priv == NULL);
	assert(device->state == DEVICE_REGISTER);

	/*
	 * Don't try to free a permanent device.
	 */
	if(F_ISSET(device->flags, DEVICE_PERM)) {
		return;
	}

	/*
	 * Inform the parent bus that this child is freed.
	 */
	if(device->parent != NULL) {
		bus_on_child_free(device->parent, device);
	}

	/*
	 * Not an intr-child anymore.
	 */
	if(device->intr_parent) {
		list_remove(&device->intr_parent->intr_children,
			&device->node_intr);
	}

	/*
	 * Free all the device properties.
	 */
	devprop_free(device);

	/*
	 * Free the device ids.
	 */
	while(device->ids) {
		device_id_t *next = device->ids->next;
		kfree(device->ids);
		device->ids = next;
	}

	list_remove(&device_list, &device->node);

	list_destroy(&device->children);
	list_destroy(&device->intr_children);
	list_destroy(&device->proplist);
	list_node_destroy(&device->node_child);
	list_node_destroy(&device->node_intr);
	list_node_destroy(&device->node);
	kfree(device);
}

device_driver_t *device_driver(device_t *device) {
	return device->driver;
}

const char *device_name(device_t *device) {
	if(device->driver && device->driver->name) {
		return device->driver->name;
	} else if(device->driver_hint) {
		return device->driver_hint;
	} else {
		return "unknown";
	}
}

bus_dev_addr_t device_addr(device_t *device) {
	return device->addr;
}

void *device_priv(device_t *device) {
	return device->priv;
}

bool device_attached(device_t *device) {
	return device->state == DEVICE_ATTACH;
}

device_t *device_get_parent(device_t *device) {
	/*
	 * Technically this sync_assert is not needed.
	 */
	sync_assert(&device_lock);
	return device->parent;
}

device_t *device_get_intr_parent(device_t *device) {
	/*
	 * Technically this sync_assert is not needed.
	 */
	sync_assert(&device_lock);
	return device->intr_parent;
}

void device_set_desc(device_t *device, const char *fmt, ...) {
	va_list ap;

	sync_assert(&device_lock);
	va_start(ap, fmt);
	vsnprintf(device->desc, sizeof(device->desc), fmt, ap);
	va_end(ap);
}

void device_set_flag(device_t *device, int flag) {
	F_SET(device->flags, flag);
}

void device_add_ref(device_t *device, device_t *ref) {
	if(device_get_parent(ref) != device) {
		device->num_ref++;
	}
}

void device_remove_ref(device_t *device, device_t *ref) {
	assert(device->num_ref > 0);

	if(device_get_parent(ref) != device) {
		device->num_ref--;
	}
}

void device_inc_res_num(device_t *device) {
	device->num_res++;
}

void device_dec_res_num(device_t *device) {
	assert(device->num_res > 0);
	device->num_res--;
}

void device_add_id(device_t *device, const char *type, uint64_t num,
	device_id_match_t match)
{
	device_id_t *id;

	/*
	 * Allocate a new device id.
	 */
	id = kmalloc(sizeof(*id), VM_WAIT);
	id->type = type;
	id->num = num;
	id->match = match;

	/*
	 * Insert the new id into the id list of the device
	 */
	id->next = device->ids;
	device->ids = id;
}

bool device_generic_match_id(uint64_t devid, uint64_t devtab_id) {
	return devid == devtab_id;
}

devtab_entry_t *device_match_devtab(device_t *device, devtab_t *devtab) {
	devtab_entry_t *entry;
	device_id_t *id;

	/*
	 * Loop through every id of the device.
	 */
	id = device->ids;
	while(id) {
		/*
		 * The id has to be of the correct type.
		 */
		if(!strcmp(id->type, devtab->type)) {
			entry = devtab->entries;
			while(entry->id != DEVTAB_END_ID) {
				if(id->match(id->num, entry->id) == true) {
					return entry;
				}

				entry++;
			}
		}

		id = id->next;
	}

	/*
	 * No match was found.
	 */
	return NULL;
}

int device_mthd(device_t *device, int mthd, void *arg) {
	device_driver_t *driver;

	assert(device->state == DEVICE_ATTACH);

	driver = device_driver(device);
	if(driver->mthd) {
		return driver->mthd(device, mthd, arg);
	} else {
		return -ENOTSUP;
	}
}

static inline bool devprop_match(devprop_t *prop, devprop_type_t type,
	const char *name)
{
	return (type == DEVPROP_ANY || prop->type == type) &&
				(name == NULL || !strcmp(prop->name, name));
}

devprop_t *device_prop_get(device_t *device, devprop_type_t type,
	const char *name)
{
	devprop_t *prop;

	kassert(name != NULL, "[device] device_prop_get: name == NULL\n");
	foreach(prop, &device->proplist) {
		if(devprop_match(prop, type, name) == true) {
			return prop;
		}
	}

	return NULL;
}


devprop_res_t *device_prop_res(device_t *device, bus_res_type_t type,
	const char *name)
{
	devprop_t *prop;

	foreach(prop, &device->proplist) {
		if(devprop_match(prop, DEVPROP_RES, name) == true &&
			prop->value.res.type == type)
		{
			return &prop->value.res;
		}
	}

	return NULL;
}

devprop_t *devprop_alloc(device_t *device, const char *name,
	devprop_type_t type)
{
	devprop_t *prop;

	/*
	 * It is possible that a bus-driver wants to add a property
	 * to a device, but the allknowing motherboard driver has
	 * overriden that property. Since the motherboard driver
	 * is considered a more reliable source of information than
	 * a bus driver and it is loaded before every other driver
	 * (except rootdev) and it added properties to devices prior
	 * to anything, a property conflict is simply be ignored.
	 */
	prop = device_prop_get(device, DEVPROP_ANY, name);
	if(prop) {
		kassert(prop->type == type, "[device] conflicting property "
			"types: expected: %d, got: %d", type, prop->type);
		return NULL;
	}

	/*
	 * Allocate a new device property.
	 */
	prop = kmalloc(sizeof(*prop), VM_WAIT);
	list_node_init(prop, &prop->node);
	prop->name = name;
	prop->type = type;
	prop->flags = 0;

	/*
	 * If a property is added before the device driver is attached
	 * the property won't be freed until the device is freed.
	 */
	if(device->state == DEVICE_ATTACH) {
		F_SET(prop->flags, DEVPROP_DETACH);
	}

	list_append(&device->proplist, &prop->node);

	/*
	 * The caller is responsible for initializing the value of the
	 * property.
	 */
	return prop;
}

static inline void devprop_free(device_t *device) {
	devprop_t *prop;

	/*
	 * Properties are only freed when the device structure itself
	 * is freed (state == REGISTER) or the device is detached
	 * (state == ATTACH).
	 */
	assert(device->state == DEVICE_REGISTER ||
		device->state == DEVICE_ATTACH);

	foreach(prop, &device->proplist) {
		if(device->state == DEVICE_REGISTER ||
			F_ISSET(prop->flags, DEVPROP_DETACH))
		{
			list_remove(&device->proplist, &prop->node);
			list_node_destroy(&prop->node);
			kfree(prop);
		}
	}
}

/**
 * @brief Free the driver private pointer of a device.
 */
static inline void device_free_priv(device_t *device) {
	if(device->priv) {
		kfree(device->priv);
		device->priv = NULL;
	}
}

static void device_change_state(device_t *device, device_state_t state) {
	switch(device->state) {
	case DEVICE_REGISTER:
		if(state != DEVICE_PROBE && state != DEVICE_ATTACH) {
			goto error;
		}
		break;
	case DEVICE_PROBE:
		if(state == DEVICE_ATTACH_LATER) {
			devices_not_attached++;
		}
		break;
	case DEVICE_ATTACH:
		if(state != DEVICE_REGISTER) {
			goto error;
		}
		break;
	case DEVICE_ATTACH_LATER:
		if(state == DEVICE_ATTACH) {
			devices_not_attached--;
		}
		break;
	default:
		kpanic("[device] unknown device state: %d", state);
	}

	device->state = state;
	return;

error:
	kpanic("[device] %s: trying to change from \"%s\" to \"%s\"",
		device_name(device), device_state_name(device->state),
		device_state_name(state));
}

int device_detach(device_t *device) {
	device_t *parent = device_get_parent(device);
	int err;

	if(device->state == DEVICE_ATTACH) {
		/*
		 * Cannot detach a device if some other devices depend on its.
		 */
		if(device->num_ref > 0) {
			return -EBUSY;
		}

		err = device->driver->detach(device);
		if(err) {
			return err;
		}

		if(device->num_res != 0) {
			kpanic("[device] %s: detach did not free every "
				"resource", device_name(device));
		}

		device->desc[0] = '\0';
		if(parent) {
			bus_on_child_detach(parent, device);
		}

		devprop_free(device);
		device_free_priv(device);
	}

	if(device->state != DEVICE_REGISTER) {
		device_change_state(device, DEVICE_REGISTER);
		if(device->driver) {
			driver_put(device->driver);
			device->driver = NULL;
			device->devtab_entry = NULL;
		}
	}

	return 0;
}

/**
 * Check if a device can be attached. This can only happen
 * if e.g. the parent interrupt controller is already
 * available.
 */
static bool device_can_attach(device_t *device) {
	device_type_t type = device->driver->type;
	int flags;

	flags = device->driver->flags;

	/*
	 * Some devices will not be loaded early.
	 */
	if(device_early) {
		if(flags & DEV_NO_EARLY) {
			return false;
		} else if(!(flags & DEV_EARLY)) {
			switch(type) {
			case DEVICE_INPUT:
			case DEVICE_NET:
			case DEVICE_GPU:
			case DEVICE_DISK:
				return false;
			default:
				break;
			}
		}
	}

	/*
	 * Don't load devices with dynamicly allocated resources yet.
	 */
	if(flags & DEV_DEP_DYN && !(device_deps & DEV_DEP_DYN)) {
		return false;
	}

	/*
	 * Check if all of the interrupt parents are attached.
	 */
	if(flags & DEV_DEP_INTR) {
		device_t *cur = device;

		for(;;) {
			if(cur->intr_parent) {
				cur = cur->intr_parent;
			} else {
				cur = cur->parent;
			}

			if(!cur || (cur->flags & DEVICE_INTR_ROOT)) {
				break;
			} else if(cur->state != DEVICE_ATTACH) {
				return false;
			}
		}
	}

	return true;
}

static int device_attach(device_t *device) {
	device_driver_t *driver;
	int err;

	driver = device->driver;
	assert(device->state == DEVICE_PROBE || device->state ==
		DEVICE_ATTACH_LATER);

	/*
	 * If the dependencies are unmet, the driver
	 * will be attached in a later device tree pass!
	 * Some device types will not be attached early.
	 */
	if(device_can_attach(device) == false) {
		kprintf("[device] delaying attach of %s\n",
			device_name(device));
		device_change_state(device, DEVICE_ATTACH_LATER);
		return 0;
	}

	kprintf("[device] attaching %s\n", device_name(device));
	if(driver->private) {
		device_alloc_priv(device, driver->private);
	}

	device_change_state(device, DEVICE_ATTACH);
	err = driver->attach(device);
	if(err) {
		if(device->num_res != 0) {
			kpanic("[device] %s: attach did not free every resource"
				" on failure", device_name(device));
		}

		device->driver = NULL;
		driver_put(driver);
		device_change_state(device, DEVICE_REGISTER);
		device_free_priv(device);

		/*
		 * No device present here so the device is not needed.
		 */
		if(err == -ENODEV) {
			bus_remove_child(device->parent, device);
		}

		return err;
	}

	return 0;
}

int device_probe_attach(device_t *device) {
	int err = 0;

	sync_assert(&device_lock);
	if(!device->driver) {
		assert(device->state == DEVICE_REGISTER);
		err = device_driver_choose(device);
	}

	if(!err && (device->state == DEVICE_PROBE || device->state ==
		DEVICE_ATTACH_LATER))
	{
		err = device_attach(device);
	}

	return err;
}

void device_new_pass(device_t *device) {
	assert(device->state == DEVICE_ATTACH);
	if(device->driver->new_pass) {
		device->driver->new_pass(device);
	}
}

void device_set_intr_parent(device_t *device, device_t *intr_parent) {
	sync_assert(&device_lock);
	assert(device->intr_parent == NULL);
	list_append(&intr_parent->intr_children, &device->node_intr);
	device->intr_parent = intr_parent;
}

void *device_alloc_priv(device_t *device, size_t size) {
	void *priv;

	sync_assert(&device_lock);
	if(device->priv != NULL) {
		kpanic("[device] %s: multiple driver private pointers",
			device_name(device));
	}

	priv = kmalloc(size, VM_WAIT);
	device->priv = priv;

	return priv;
}

device_t *device_find_type(device_t *start, device_type_t type,
	const char *driver)
{
	device_t *cur;

	if(start == NULL) {
		cur = list_first(&device_list);
	} else {
		cur = list_next(&device_list, &start->node);
	}

	while(cur) {
		/*
		 * TODO rename drv_hint,
		 * since hint is wrong
		 */
		if((cur->driver == NULL && cur->driver_hint &&
			!strcmp(cur->driver_hint, driver)) ||
			(cur->driver && cur->driver->type == type &&
			!strcmp(cur->driver->name, driver)))
		{
			return cur;
		}

		cur = list_next(&device_list, &cur->node);
	}

	return NULL;
}

static void devtree_print_recur(device_t *device, int indent) {
	device_t *child;

	for(int i = 0; i < indent; i++) {
		kprintf("    ");
	}

	if(device->state == DEVICE_ATTACH) {
		kprintf("%s@0x%llx", device_name(device), device->addr);
		if(device->desc[0]) {
			kprintf(": %s\n", device->desc);
		} else if(device->state != DEVICE_REGISTER) {
			kprintf("\n");
		} else {
			return;
		}
	} else if(device->driver_hint) {
		kprintf("%s (not attached)\n", device->driver_hint);
	} else if(device->desc[0]) {
		kprintf("%s\n", device->desc);
	} else {
		kprintf("untitled\n");
	}

	indent++;
	foreach(child, &device->children) {
		devtree_print_recur(child, indent);
	}
}

void devtree_print(void) {
	devtree_print_recur(root_device, 0);
}

static void device_do_passes(void) {
	size_t prev;

	do {
		prev = devices_not_attached;
		device_new_pass(root_device);
	/*
	 * TODO instead of checking if the amount of
	 * not attached devices change, use sth like
	 * device_attached_num to check if the number
	 * of attached devices changes.
	 */
	} while(prev != devices_not_attached);
}

void __init init_device_early(void) {
	arch_device_init();
	device_driver_init();

	/*
	 * Initialization currently is not multithreaded, but
	 * make sure no sync_assert fails.
	 */
	sync_scope_acquire(&device_lock);

	root_device = bus_add_child_at(NULL, "root", 0);
	device_probe_attach(root_device);
	device_do_passes();

	device_early = false;
}

void __init init_device(void) {
	sync_scope_acquire(&device_lock);

	device_do_passes();
	device_deps |= DEV_DEP_DYN;
	device_do_passes();
	devtree_print();
}

export(device_set_desc);
export(device_alloc_priv);
export(device_add_ref);
export(device_remove_ref);

export(device_add_id);
export(device_generic_match_id);

export(device_set_intr_parent);
export(device_get_intr_parent);
export(device_get_parent);

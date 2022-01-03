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
#include <kern/symbol.h>
#include <vm/malloc.h>
#include <device/device.h>
#include <device/_device.h>

/**
 * @brief List entry in the resource private data list for busses.
 */
typedef struct bus_res_priv {
	struct bus_res_priv *next;
	device_t *bus;
	char data[];
} bus_res_priv_t;

const char *bus_res_type_name(bus_res_type_t type) {
	switch(type) {
	case BUS_RES_MEM:
		return "mem";
	case BUS_RES_IO:
		return "io";
	case BUS_RES_INTR:
		return "intr";
	default:
		kpanic("[bus] invalid bus_res_type_t: %d", type);
	}
}

static bus_driver_t *bus_get_driver(device_t *bus) {
	return bus->driver->bus;
}

device_t *bus_device_parent(device_t *device, bus_res_type_t type) {
	device_t *parent = NULL;

	if(type == BUS_RES_INTR) {
		/*
		 * Redirect interrupt resources to interrupt-parent.
		 */
		parent = device_get_intr_parent(device);
		if(parent) {
			if(parent->state != DEVICE_ATTACH) {
				kpanic("[bus] %s: trying to allocating "
					"interrupt too early",
					device_name(device));
			}

			return parent;
		}
	}

	return device_get_parent(device);
}

device_t *bus_child_next(device_t *bus, device_t *prev) {
	if(prev == NULL) {
		return list_first(&bus->children);
	} else {
		return list_next(&bus->children, &prev->node_child);
	}
}

void bus_insert_child(device_t *bus, device_t *child, bus_dev_addr_t addr) {
	/*
	 * If a bus does not have any specific device addresses, a bus driver
         * can set the address of the children to BUS_DEV_ADDR_ANY. This is
         * however not optimal for debugging and thus BUS_DEV_ADDR_ANY is
         * converted to a more usable "address".
	 */
	if(addr == BUS_DEV_ADDR_ANY) {
		addr = bus ? list_length(&bus->children) : 0;
	}

	if(bus) {
		list_append(&bus->children, &child->node_child);

		/*
		 * Inherit the dma restrictions and the dma callbacks from the
		 * parent device.
		 */
		assert(bus->dma);
		child->dma = bus->dma;
	}

	child->addr = addr;
	child->parent = bus;
}

device_t *bus_add_child_at(device_t *parent, const char *drv_hint,
	bus_dev_addr_t addr)
{
	device_t *device;

	sync_assert(&device_lock);
	device = device_alloc(drv_hint);
	bus_insert_child(parent, device, addr);

	return device;
}

device_t *bus_get_child(device_t *bus, bus_dev_addr_t addr) {
	device_t *child;

	foreach(child, &bus->children) {
		if(child->addr == addr) {
			return child;
		}
	}

	return NULL;
}

void bus_remove_children(device_t *bus) {
	device_t *child;

	foreach(child, &bus->children) {
		bus_remove_child(bus, child);
	}
}

void bus_remove_child(device_t *bus, device_t *child) {
	sync_assert(&device_lock);

	/*
	 * Cannot use bus_remove_child if the child is probed /
	 * attached!
	 */
	assert(child->state == DEVICE_REGISTER);

	if(!F_ISSET(child->flags, DEVICE_PERM)) {
		list_remove(&bus->children, &child->node_child);
		device_free(child);
	}
}

void bus_probe_children(device_t *bus) {
	device_t *child;

	sync_assert(&device_lock);

	/*
	 * Probe every child registered using bus_add_child.
	 * Using the safe foreach version here since device_probe_attach
	 * may free the device.
	 */
	foreach(child, &bus->children) {
		device_probe_attach(child);
	}
}

void bus_set_child_priv(device_t *child, void *priv) {
	if(priv != NULL) {
		kassert(child->bus_priv == NULL, "[bus] multiple bus_priv "
			"pointers");
	}

	child->bus_priv = priv;
}

void *bus_alloc_child_priv(device_t *child, size_t size) {
	void *priv;

	priv = kmalloc(size, VM_WAIT);
	bus_set_child_priv(child, priv);

	return priv;
}

void bus_free_child_priv(device_t *child) {
	if(!child->bus_priv) {
		kpanic("[device] no bus_priv pointer");
	}

	kfree(child->bus_priv);
	bus_set_child_priv(child, NULL);
}

void *bus_child_priv(device_t *child) {
	return child->bus_priv;
}

void bus_on_child_detach(device_t *device, device_t *child) {
	bus_driver_t *driver = bus_get_driver(device);
	assert(driver);

	if(driver->bus_child_detach) {
		driver->bus_child_detach(device, child);
	}
}

void bus_on_child_free(device_t *device, device_t *child) {
	bus_driver_t *driver = bus_get_driver(device);
	assert(driver);

	if(driver->bus_child_free) {
		driver->bus_child_free(device, child);
	}

	if(bus_child_priv(child) != NULL) {
		kpanic("[bus] %s did not free a child's bus-private structure",
			device_name(device));
	}
}

void *bus_res_add_priv(device_t *device, bus_res_t *res, size_t size) {
	bus_res_priv_t *priv;

	priv = kmalloc(size + sizeof(bus_res_priv_t), VM_WAIT);
	priv->bus = device;

	/*
	 * Each resource has a list of private bus data. Insert the
	 * structure into that list.
	 */
	assert(res->priv == NULL);
	priv->next = res->priv;
	res->priv = priv;

	return priv->data;
}

void *bus_res_get_priv(device_t *bus, bus_res_t *res) {
	bus_res_priv_t *priv;

	priv = res->priv;
	while(priv) {
		if(priv->bus == bus) {
			return priv->data;
		} else {
			priv = priv->next;
		}
	}

	kpanic("[bus] no private resource data found for bus \"%s\"",
		device_name(bus));
}

static void bus_res_free_priv(bus_res_t *res) {
	bus_res_priv_t *priv, *old;

	priv = res->priv;
	while(priv) {
		old = priv;
		priv = priv->next;
		kfree(old);
	}
}

void bus_adjust_resource(device_t *device, const char *name,
	bus_res_req_t *req)
{
	devprop_res_t *res;

	/*
	 * Don't adjust the resource, if the driver provided
	 * start, end or size!
	 */
	if(!BUS_ADDR_ANY(req->addr, req->end, req->size)) {
		return;
	}

	kassert(req->align == 1, "[bus] invalid alignment for default "
		"resource: %d\n", req->align);

	res = device_prop_res(device, req->type, name);
	if(res != NULL) {
		kassert(req->type == res->type, "[bus] resource type mismatch: "
			"expected: %d got: %d", req->type, res->type);

		req->addr = res->addr;
		req->size = res->size;
		req->end = res->addr - 1 + res->size;
	}
}

static int bus_common_alloc_res(device_t *device, bus_res_type_t type,
	int bus_id, const char *name, bus_addr_t start, bus_addr_t end,
	bus_size_t size, bus_addr_t align, int flags, bus_res_t *res)
{
	bus_res_req_t req;
	int err;

	/*
	 * Only IO and MEM resources can be mapped.
	 */
	if(type == BUS_RES_INTR && F_ISSET(flags, RF_MAP)) {
		kprintf("[bus] %s: bus_alloc_res: RF_MAP not allowed for "
			"interrupts\n", device_name(device));
		return -EINVAL;
	}

	/*
	 * Build the request and initialize the resource structure.
	 */
	req.type = type;
	req.addr = start;
	req.end = end;
	req.size = size;
	req.align = align;
	req.bus_id = bus_id;
	req.res = res;

	res->setup = false;
	res->device = device;
	res->type = type;
	res->priv = NULL;
	if(type == BUS_RES_INTR) {
		res->intr.flags = 0; /* TODO */
	}

	/*
	 * Search for resource hints, which override the resources
	 * address etc.
	 */
	if(BUS_ADDR_ANY(start, end, size)) {
		bus_adjust_resource(device, name, &req);
	}

	err = bus_generic_alloc_res(device, &req);
	if(err) {
		kprintf("[bus] %s: allocating resource failed\n",
 			device_name(device));
		return err;
	}

	/*
	 * Map the resource if requested.
	 */
	if(F_ISSET(flags, RF_MAP)) {
		err = bus_map_res(device, res);
		if(err) {
			bus_free_res(device, res);
			return err;
		}
	}

	device_printf(device, "allocated %s resource: 0x%x - 0x%x\n",
		bus_res_type_name(type), bus_res_get_addr(res),
		bus_res_get_end(res));
	device_inc_res_num(device);

	return 0;
}

int bus_alloc_res_at(device_t *device, bus_res_type_t type, int bus_id,
	bus_addr_t start, bus_addr_t end, bus_size_t size, bus_addr_t align,
	int flags, bus_res_t *res)
{
	return bus_common_alloc_res(device, type, bus_id, NULL, start, end,
		size, align, flags, res);
}

int bus_alloc_res_fixed(device_t *device, bus_res_type_t type, int bus_id,
	bus_addr_t start, bus_size_t size, int flags, bus_res_t *res)
{
	return bus_alloc_res_at(device, type, bus_id, start, start + size - 1,
			size, 0x1, flags, res);
}

int bus_alloc_res_name(device_t *device, bus_res_type_t type, int bus_id,
	const char *name, int flags, bus_res_t *res)
{
	return bus_common_alloc_res(device, type, bus_id, name, 0, BUS_ADDR_MAX,
			BUS_SIZE_MAX, 1, flags, res);
}

int bus_alloc_res(device_t *device, bus_res_type_t type, int bus_id, int flags,
		bus_res_t *res)
{
	return bus_alloc_res_name(device, type, bus_id, NULL, flags, res);
}

void bus_free_res(device_t *device, bus_res_t *res) {
	/*
	 * Automatically teardown the resource if it is still
	 * mapped/setup.
	 */
	if(res->setup) {
		if(res->type == BUS_RES_INTR) {
			bus_teardown_intr(device, res);
		} else {
			bus_unmap_res(device, res);
		}
	}

	/*
	 * Send the request up the device tree and do some cleanups.
	 */
	bus_generic_free_res(device, res);
	device_dec_res_num(device);
	bus_res_free_priv(res);
}

int bus_map_res(device_t *device, bus_res_t *res) {
	int err;

	err = bus_generic_setup_res(device, res);
	if(!err) {
		res->setup = true;
	} else {
		kprintf("[bus] %s: bus_map_res failed\n", device_name(device));
	}

	return err;
}

void bus_unmap_res(device_t *device, bus_res_t *res) {
	bus_generic_teardown_res(device, res);
	res->map.acc = NULL;
	res->map.map = NULL;
	res->map.map_dev = NULL;
	res->setup = false;
}

int bus_setup_intr(device_t *device, bus_intr_hand_t hand,
	bus_intr_thand_t thand, void *arg, bus_res_t *res)
{
	int err;

	list_node_init(res, &res->intr.node);
	res->intr.hand = hand;
	res->intr.thand = thand;
	res->intr.arg = arg;

	err = bus_generic_setup_res(device, res);
	if(!err) {
		res->setup = true;
	} else {
		list_node_destroy(&res->intr.node);
		kprintf("[bus] %s: bus_setup_intr failed\n",
			device_name(device));
	}

	return err;
}

void bus_teardown_intr(device_t *device, bus_res_t *res) {
	bus_generic_teardown_res(device, res);
	list_node_destroy(&res->intr.node);
	res->setup = false;
}

int bus_generic_detach(device_t *device) {
	device_t *child;
	size_t num_fail;
	int err;

	num_fail = list_length(&device->children);
	do {
		size_t nfail = 0;

		/*
		 * Loop through the children and try to detach every
		 * one of them.
		 */
		foreach(child, &device->children) {
			err = device_detach(child);
			if(err) {
				nfail++;
			}
		}

		/*
		 * If the nuber of fails is the same as the last iteration
		 * there is no way to detach!
		 */
		if(nfail == num_fail) {
			return -EBUSY;
		}

		num_fail = nfail;
	} while(num_fail);

	/*
	 * Success. So let's free the child devices.
	 */
	foreach(child, &device->children) {
		bus_remove_child(device, child);
	}

	return 0;
}

int bus_generic_alloc_res(device_t *bus, bus_res_req_t *req) {
	device_t *parent = bus_device_parent(bus, req->type);
	int err;

	sync_assert(&device_lock);
	assert(req->align >= 1);

	err = BUS_ALLOC_RES(parent, req);
	if(err) {
		kprintf("[bus] %s: allocating resource for %s failed\n",
			device_name(parent), device_name(req->res->device));
	}

	return err;
}

void bus_generic_new_pass(device_t *bus) {
	device_t *child;

	sync_assert(&device_lock);
	foreach(child, &bus->children) {
		if(child->state == DEVICE_ATTACH) {
			device_new_pass(child);
		} else {
			device_probe_attach(child);
		}
	}
}

/*
 * default bus callback implementations.
 */
export(bus_generic_new_pass);
export(bus_generic_alloc_res);
export(bus_generic_detach);

/*
 * device interface.
 */
export(bus_alloc_res_at);
export(bus_alloc_res_fixed);
export(bus_alloc_res_name);
export(bus_alloc_res);
export(bus_free_res);
export(bus_map_res);
export(bus_unmap_res);
export(bus_setup_intr);
export(bus_teardown_intr);

export(bus_add_child_at);
export(bus_probe_children);
export(bus_remove_child);

export(bus_get_driver);
export(bus_device_parent);

export(bus_set_child_priv);
export(bus_alloc_child_priv);
export(bus_free_child_priv);
export(bus_child_priv);
export(bus_get_child);

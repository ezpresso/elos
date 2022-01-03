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
#include <kern/critical.h>
#include <kern/sched.h>
#include <kern/cpu.h>
#include <device/device.h>
#include <device/dma.h>
#include <vm/kern.h>
#include <arch/bus.h>
#include <arch/interrupt.h>
#include <arch/lapic.h>

static resman_t root_resman[BUS_RES_MAX];
static resman_t syscall_rsvd;
static bool root_attached = false;
static bus_dma_engine_t root_dma;

/*
 * TODO remove
 *
 * Maybe provide helpers for both access methods, and let the mboard
 * driver decide.
 */
void arch_device_init(void) {
	int err;

	err = arch_pci_init();
	if(err) {
		kpanic("arch_pci_init failed");
	}
}

static void rootdev_intr_handler(int intr, __unused struct trapframe * r,
	void *arg)
{
	/*
	 * The critical section would technically not be needed, however
	 * it prevents device-drivers from waiting inside an interrupt
	 * handler, because the waiting/mutex interfaces panic if called
	 * inside a critical section.
	 */
	critical {
		bus_call_intr_handler((bus_res_t *)arg, intr);
	}

	lapic_eoi();
}

static int rootdev_alloc_res(__unused device_t *device, bus_res_req_t *req) {
	int err;

	/*
	 * Dont't support dynamic allocations yet. TODO except for interupts
	 */
	if(req->bus_id != 0 || (req->type != BUS_RES_INTR &&
		BUS_ADDR_ANYWHERE(req->addr, req->end, req->size)) ||
		BUS_ADDR_ANY(req->addr, req->end, req->size))
	{
		return -EINVAL;
	}

	err = resman_alloc_range(&root_resman[req->type], &req->res->res,
		req->addr, req->end, req->size, req->align);
	if(err) {
		return err;
	}

	return 0;
}

static void rootdev_free_res(__unused device_t *device, bus_res_t *res) {
	resman_free(&res->res);
}

static int rootdev_setup_res(device_t *roodev, bus_res_t *res) {
	switch(res->type) {
	case BUS_RES_IO:
		bus_res_set_acc(roodev, res, &x86_io_acc);
		return 0;
	case BUS_RES_MEM:
		res->map.map = vm_mapdev(bus_res_get_addr(res),
			bus_res_get_size(res), VM_MEMATTR_UNCACHEABLE);
		bus_res_set_acc(roodev, res, &x86_mem_acc);
		return 0;
	case BUS_RES_INTR: {
		bus_size_t i, size, addr;
	
		assert(!res->intr.thand);
		size = bus_res_get_size(res);
		addr = bus_res_get_addr(res);

		for(i = 0; i < size; i++) {
			cpu_set_intr_handler(addr + i, rootdev_intr_handler,
				res);
		}

		break;
	}
	default:
		kpanic("[rootdev] invalid resource type: %d", res->type);
	}

	return 0;
}

static void rootdev_teardown_res(__unused device_t *roodev, bus_res_t *res) {
	switch(res->type) {	
	case BUS_RES_MEM:
		vm_unmapdev(res->map.map, bus_res_get_size(res));
		break;
	case BUS_RES_INTR: {
		bus_size_t i, size, addr;

		size = bus_res_get_size(res);
		addr = bus_res_get_addr(res);

		for(i = 0; i < size; i++) {
			cpu_set_intr_handler(addr + i, NULL, NULL);
		}

		break;
	}
	default:
		break;
	}
}

static int rootdev_probe(__unused device_t *device) {
	/*
	 * Should actually panic here if the root device is
	 * already attached.
	 */
	return root_attached ? DEVICE_PROBE_FAIL : DEVICE_PROBE_OK;
}

static int rootdev_attach(device_t *device) {
	root_attached = true;

	/*
	 * Create a bounce buffer dma engine.
	 */
	bus_dma_create_engine(device,
			0x0, /* start */
			BUS_ADDR_MAX, /* end */
			BUS_SIZE_MAX, /* maxsz */
			0x1, /* align */
			0x0, /* boundary */
			&bus_dma_bounce_ops,
			&root_dma
		);

	/*
	 * 16 bit io space: 0x0 - 0xFFFF.
	 */
	resman_init_root(&root_resman[BUS_RES_IO], 0, 0xFFFF);
	
	/*
	 * 32bit address space: 0x0 - 0xFFFFFFFF.
	 */
	resman_init_root(&root_resman[BUS_RES_MEM], 0, UINT32_MAX);

	/*
	 * Some interrupts can be used by devices. This excludes the
	 * interrupt numbers for exceptions and the interrupts that
	 * are used for the various IPIs.
	 */
	resman_init_root(&root_resman[BUS_RES_INTR], INT_DEV_START,
		INT_DEV_END);
	resman_reserve(&root_resman[BUS_RES_INTR], &syscall_rsvd, INT_SYSCALL,
		0x1);

	device_set_desc(device, "Root device");
	bus_add_child(device, "qemu-i440fx");
	bus_probe_children(device);

	return 0;
}

static int rootdev_detach(__unused device_t *device) {
	int err = bus_generic_detach(device);
	if(err) {
		return err;
	}

	bus_dma_destroy_engine(&root_dma);

	device_printf(device, "io resman:\n");
	resman_print(&root_resman[BUS_RES_IO]);
	device_printf(device, "end\n");

	device_printf(device, "mem resman:\n");
	resman_print(&root_resman[BUS_RES_MEM]);
	device_printf(device, "end\n");

	device_printf(device, "intr resman:\n");
	resman_print(&root_resman[BUS_RES_INTR]);
	device_printf(device, "end\n");

	for(size_t i = 0; i < NELEM(root_resman); i++) {
		resman_destroy_root(&root_resman[i]);
	}

	return 0;
}

static bus_driver_t rootdev_bus_driver = {
	.bus_child_detach = NULL,
	.bus_alloc_res = rootdev_alloc_res,
	.bus_free_res = rootdev_free_res,
	.bus_setup_res = rootdev_setup_res,
	.bus_teardown_res = rootdev_teardown_res,
};

static DRIVER(rootdev_driver) {
	.name = "root",
	.flags = 0,
	.type = DEVICE_ROOT,
	.probe = rootdev_probe,
	.attach = rootdev_attach,
	.detach = rootdev_detach,
	.new_pass = bus_generic_new_pass,
	.bus = &rootdev_bus_driver,
};

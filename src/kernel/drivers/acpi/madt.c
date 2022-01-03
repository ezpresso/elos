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
#include <kern/cpu.h>
#include <device/device.h>
#include <vm/malloc.h>
#include <acpi/table.h>

#define ISA_IRQ_NUM 16

#define MADT_PROC		0
#define MADT_IOAPIC 		1
#define MADT_INT_OVERRIDE 	2
#define MADT_LAPIC_NMI		4

#define MADT_POLARITY_MASK	(3 << 0)
#define MADT_POLARITY_ACTIVE_HI	(1 << 0)
#define MADT_POLARITY_ACTIVE_LO	(3 << 0)
#define MADT_TRIGGER_MASK	(3 << 2)
#define MADT_TRIGGER_EDGE	(1 << 2)
#define MADT_TRIGGER_LEVEL	(3 << 2)

typedef struct madt_entry {
	uint8_t type;
	uint8_t len;

	union {
		struct {
			uint8_t acpi_id; /* ACPI Processor ID */
			uint8_t apic_id;
			uint32_t flags;
		} __packed proc;

		struct {
			uint8_t id;
			uint8_t reserved;
			uint32_t addr;
			uint32_t gsib;	/* Global System Interrupt Base */
		} __packed ioapic;

		struct {
			uint8_t bus;
			uint8_t src;
			uint32_t intr;
			uint8_t polarity	:2;
			uint8_t mode		:2;
			uint16_t rsvd		:12;
		} __packed int_override;

		struct {
			uint8_t id;
			uint16_t flags;
			uint8_t lint;
		} __packed lapic_nmi;
	};
} __packed madt_entry_t;

typedef struct {
	acpi_sdt_hdr_t hdr;
	uint32_t local_controller_addr;

#define PCAT_COMPAT (1 << 0)
	uint32_t flags;
} __packed acpi_madt_t;

typedef struct acpi_intr_cntlr {
	list_node_t node;
	device_t *device;
	uint32_t gsib;
} acpi_intr_cntlr_t;

typedef struct madt_res_priv {
	acpi_intr_cntlr_t *cntlr;
} madt_res_priv_t;

static DEFINE_LIST(madt_cntlr_list);
static int madt_intr_remap[ISA_IRQ_NUM];
static bool madt_attached = false;

/**
 * @brief Route an interrupt to the appropriate controller.
 *
 * As an example we have 2 interrupt controllers, one managing
 * interrupts 0-23 and the other one 24-31. In this case, interrupt
 * #3 would be translated to interrupt #3 on controller #1 and interrupt
 * #27 would  be translated to interrupt #3 on controller #2.
 */
static acpi_intr_cntlr_t *madt_intr_route(bus_addr_t *addrp) {
	acpi_intr_cntlr_t *cntlr, *out = NULL;
	bus_addr_t addr = *addrp;

	foreach(cntlr, &madt_cntlr_list) {
		if(addr >= cntlr->gsib && (out == NULL ||
			cntlr->gsib > out->gsib))
		{
			out = cntlr;
		}
	}

	if(out) {
		return *addrp = addr - out->gsib, out;
	} else {
		return NULL;
	}
}

static device_t *madt_get_intr_cntlr(device_t *device, bus_res_t *res) {
	madt_res_priv_t *priv = bus_res_get_priv(device, res);
	return priv->cntlr->device;
}

static int madt_alloc_res(device_t *device, bus_res_req_t *req) {
	acpi_intr_cntlr_t *cntlr;
	madt_res_priv_t *priv;

	if(req->type != BUS_RES_INTR) {
		return bus_generic_alloc_res(device, req);
	}

	assert(BUS_ADDR_FIXED(req->addr, req->end, req->size));
	assert(req->size == 1);
	if(req->addr <= ISA_IRQ_NUM) {
		req->addr = req->end = madt_intr_remap[req->addr];
	}

	/*
	 * Redirect the request to the appropriate interrupt
	 * controller. The interrupt controller is saved in the
	 * bus private data of the resource.
	 */
	cntlr = madt_intr_route(&req->addr);
	kassert(cntlr, "[acpi] madt: no controller found for INTR%d\n",
		req->addr);

	priv = bus_res_add_priv(device, req->res, sizeof(*priv));
	priv->cntlr = cntlr;

	return BUS_ALLOC_RES(cntlr->device, req);
}

static void madt_free_res(device_t *device, bus_res_t *res) {
	if(res->type == BUS_RES_INTR) {
		BUS_FREE_RES(madt_get_intr_cntlr(device, res), res);
	} else {
		bus_generic_free_res(device, res);
	}
}

static int madt_setup_res(device_t *device, bus_res_t *res) {
	if(res->type == BUS_RES_INTR) {
		return BUS_SETUP_RES(madt_get_intr_cntlr(device, res), res);
	} else {
		return bus_generic_setup_res(device, res);
	}
}

static void madt_teardown_res(device_t *device, bus_res_t *res) {
	if(res->type == BUS_RES_INTR) {
		BUS_TEARDOWN_RES(madt_get_intr_cntlr(device, res), res);
	} else {
		bus_generic_teardown_res(device, res);
	}
}

static int madt_probe(__unused device_t *device) {
	assert(!madt_attached);
	return DEVICE_PROBE_OK;
}

static int madt_attach(device_t *device) {
	size_t nioapic = 0, ncpu = 0;
	madt_entry_t *entry, *end;
	acpi_intr_cntlr_t *cntlr;
	acpi_madt_t *madt;

	for(size_t i = 0; i < ISA_IRQ_NUM; i++) {
		madt_intr_remap[i] = i;
	}

	/*
	 * XXX Kinda not true, but solves a big issue.
	 */
	device_set_flag(device, DEVICE_INTR_ROOT);

	/*
	 * The root acpi driver provides the sdt-header via the bus
	 * private pointer.
	 */
	madt = bus_child_priv(device);
	assert(madt);

	entry = (void *)&madt[1];
	end = (void *)madt + madt->hdr.length;

	while(entry < end) {
		switch(entry->type) {
		case MADT_PROC:
			kprintf("[acpi] madt: proc: %d\n", entry->proc.apic_id);

			/*
			 * The first cpu in the list is the BSP.
			 */
			cpu_register(entry->proc.apic_id, ncpu == 0);
			ncpu++;
			break;
		case MADT_IOAPIC: {
			nioapic++;

			/*
			 * Allocate a structure for a new interrupt controller.
			 */
			cntlr = kmalloc(sizeof(acpi_intr_cntlr_t), VM_WAIT);
			list_node_init(cntlr, &cntlr->node);
			cntlr->gsib = entry->ioapic.gsib;
			cntlr->device = bus_add_child(device, "ioapic");

			/*
			 * Tell the IO-APIC which mmio address it uses. Since
			 * motherboard developers are free to choose the
			 * address, this information can only be obtained using
			 * the MADT (except if you're writing mobo-drivers).
			 */
			device_prop_add_res(cntlr->device, "mmio", BUS_RES_MEM,
				entry->ioapic.addr, PAGE_SZ);

			/*
			 * The IO-APIC takes the interrupts from the devices and
			 * delivers them to the cpus. The root_device is the
			 * place where the IO-APIC driver can allocate the
			 * cpu interrupts.
			 */
			device_set_intr_parent(cntlr->device, root_device);
			list_append(&madt_cntlr_list, &cntlr->node);

			break;
		} case MADT_INT_OVERRIDE:
			if(entry->int_override.src >= ISA_IRQ_NUM) {
				device_printf(device, "invalid interrupt "
					"override: %d -> %d\n",
					entry->int_override.src,
					entry->int_override.intr);
			} else if(entry->int_override.src !=
				entry->int_override.intr)
			{
				madt_intr_remap[entry->int_override.src] =
					entry->int_override.intr;
				device_printf(device, "remaping IRQ %d to %d\n",
					entry->int_override.src,
					entry->int_override.intr);
			}

			break;
		case MADT_LAPIC_NMI:
			break;
		default:
			kprintf("[acpi] madt: warning: unknown entry %d\n",
				entry->type);
		}

		entry = (void *)entry + entry->len;
	}

	/*
	 * Technically one could use the legacy interrupt controller if no
	 * I/O-APIC was found. However nearly nobody cares about such old
	 * hardware and thus refuse to boot in this case.
	 */
	if(nioapic == 0) {
		kpanic("[acpi] madt: no I/O-APIC found");
	}

	madt_attached = true;
	bus_probe_children(device);

	return 0;
}

static int madt_detach(device_t *device) {
	acpi_intr_cntlr_t *cntlr;
	int err;

	err = bus_generic_detach(device);
	if(err) {
		return err;
	}

	while((cntlr = list_pop_front(&madt_cntlr_list))) {
		list_node_destroy(&cntlr->node);
		kfree(cntlr);
	}

	madt_attached = false;
	return 0;
}

static bus_driver_t madt_bus = {
	.bus_alloc_res = madt_alloc_res,
	.bus_free_res = madt_free_res,
	.bus_setup_res = madt_setup_res,
	.bus_teardown_res = madt_teardown_res,
};

static DRIVER(madt_driver) {
	.name = "acpi-madt",
	.flags = 0,
	.type = DEVICE_CHIPSET,
	.private = 0,
	.devtab = NULL,
	.probe = madt_probe,
	.attach = madt_attach,
	.detach = madt_detach,
	.bus = &madt_bus,
};

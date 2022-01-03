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
#include <kern/init.h>
#include <acpi/acpi.h>
#include <acpi/table.h>
#include <arch/acpi.h>
#include <lib/unaligned.h>
#include <lib/checksum.h>
#include <lib/string.h>
#include <device/device.h>
#include <drivers/acpi.h>
#include <vm/kern.h>

#define ACPI_SDT_MAX 32

static __initdata acpi_sdt_hdr_t *acpi_sdt_list[ACPI_SDT_MAX];
static size_t acpi_sdt_num;

static acpi_sdt_hdr_t *acpi_sdt_search(const char *name) {
	for(size_t i = 0; i < acpi_sdt_num; i++) {
		if(!memcmp(name, acpi_sdt_list[i]->sig, 4)) {
			return acpi_sdt_list[i];
		}
	}

	return NULL;
}

static acpi_sdt_hdr_t *acpi_map_sdt(uint64_t phys) {
	acpi_sdt_hdr_t *header;
	size_t off;
	void *map;

	/*
	 * If an SDT lies > 4GB it cannot be accessed when
	 * running in 32-bit mode.
	 */
	if(phys > VM_PHYS_MAX) {
		return NULL;
	}

	/*
	 * Map the SDT into memory. Since we cannot access the length field of
	 * the header, we just assume that the header fits inside one page.
	 * If it becomes apparent that it does not fit in one page after
	 * mapping, we simply remap.
	 */
	vm_kern_map_phys(phys & PAGE_MASK, PAGE_SZ, VM_PROT_RD | VM_WAIT, &map);

	off = phys & ~PAGE_MASK;
	header = map + off;
	if(!INSIDE_PAGE(header, header->length)) {
		size_t size = ALIGN(header->length + off, PAGE_SZ);

		vm_kern_unmap_phys(map, PAGE_SZ);
		vm_kern_map_phys(phys & PAGE_MASK, size, VM_PROT_RD | VM_WAIT,
			&map);
		header = map + off;
	}

	return header;
}

static int acpi_parse_rsdp(void) {
	acpi_sdt_hdr_t *header;
	acpi_rsdp_t *rsdp;
	size_t ptrlen, len;
	uint64_t phys;
	size_t num;
	void *ptr;

	rsdp = acpi_get_rsdp();
	if(rsdp == NULL) {
		kprintf("[acpi] no RSDP found\n");
		return -1;
	}

	/*
	 * Check if the RSDP is even valid.
	 */
    if (rsdp->rev == ACPI_RSDP_1) {
        len = ACPI_RSDP_1_LEN;
        phys = rsdp->rsdt;
    } else {
        len = rsdp->length;
        phys = rsdp->xsdt;
    }

	if(checksum(rsdp, len) != 0) {
		kprintf("[acpi] RSDP: invalid checksum\n");
		return -1;
	}

	/*
	 * Map the R/X-SDT. This operation might fail in case
	 * the XSDT lies > 4GB it cannot be accessed when
	 * running in 32-bit mode.
	 *
	 * TODO
	 * Question is if one can rely on the RSDT being valid
	 * when an XSDT is present. In this case we could just
	 * fall back to the good ole RSDT.
	 */
	header = acpi_map_sdt(phys);
	if(header == NULL) {
		kprintf("[acpi] cannot access R/X-SDT\n");
		return -1;
	}

	ptr = (void *)header + sizeof(*header);
	if(rsdp->rev == ACPI_RSDP_1) {
		ptrlen = sizeof(uint32_t);
	} else {
		ptrlen = sizeof(uint64_t);
	}

	num = (header->length - sizeof(*header)) / ptrlen;
	for(size_t i = 0; i < num; i++) {
		acpi_sdt_hdr_t *sdt;
		uint64_t phys;

		/*
		 * Why would anyone care about alignment? ...
		 * The problem is that the header is unaligned
		 * in QEMU (or I'm retarded).
		 */
		if(rsdp->rev == ACPI_RSDP_1) {
			phys = unaligned_read32(ptr);
		} else {
			phys = unaligned_read64(ptr);
		}

		sdt = acpi_map_sdt(phys);
		if(sdt) {
			kprintf("[acpi] found SDT: %c%c%c%c\n",
				sdt->sig[0], sdt->sig[1], sdt->sig[2],
				sdt->sig[3]);

			acpi_sdt_list[acpi_sdt_num++] = sdt;
			if(acpi_sdt_num == ACPI_SDT_MAX) {
				return 0;
			}
		}

		ptr += ptrlen;
	}

	return 0;
}

bool acpi_attach_madt(device_t *device) {
	acpi_sdt_hdr_t *hdr;
	device_t *child;
	int err;

	hdr = acpi_sdt_search("APIC");
	if(hdr == NULL) {
		return false;
	}

	child = bus_add_child(device, "acpi-madt");
	bus_set_child_priv(child, hdr);

	err = device_probe_attach(child);
	if(err) {
		bus_remove_child(device, child);
		return false;
	} else {
		device_set_intr_parent(device, child);
		return true;
	}
}

#include <acpi/aml.h>

void acpi_test(void) {
	acpi_fadt_t *fadt;
	acpi_sdt_hdr_t *dsdt;

	fadt = (acpi_fadt_t *)acpi_sdt_search("FACP");
	assert(fadt);

	dsdt = acpi_map_sdt(fadt->dsdt);
	aml_parse_dsdt((void *)dsdt + sizeof(acpi_sdt_hdr_t),
		dsdt->length - sizeof(acpi_sdt_hdr_t));
}

static __init int acpi_init(void) {
	int err;

	err = acpi_parse_rsdp();
	if(err) {
		return INIT_PANIC;
	}

	return INIT_OK;
}

early_initcall(acpi_init);

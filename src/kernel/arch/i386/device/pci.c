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
#include <arch/x86.h>

/* Type 2 */
#define PCI_IOADDR(devfn, where) ((0xC000 | ((devfn & 0x78) << 5)) + where)
#define PCI_FUNC(devfn)	(((devfn & 7) << 1) | 0xf0)

/* Type 1 */
#define PCI_ADDRESS(bus, devfn, reg) \
	(0x80000000 | (bus << 16) | (devfn <<  8) | (reg & 0xFC))

typedef struct pci_access {
	uint32_t(*read) (uint8_t, uint8_t, uint8_t, uint8_t);
	void (*write) (uint8_t, uint8_t, uint8_t, uint8_t, uint32_t);
} pci_access_t;

static pci_access_t *pci_access;

static uint32_t pci_read_conf1(uint8_t bus, uint8_t devfn, uint8_t offset,
	uint8_t len)
{
	outl(0xCF8, PCI_ADDRESS(bus, devfn, offset));

	switch(len) {
	case 1: return inb(0xCFC + (offset & 3));
	case 2: return inw(0xCFC + (offset & 2));
	case 4: return inl(0xCFC);
	default: return 0xFFFF;
	}
}

static void pci_write_conf1(uint8_t bus, uint8_t devfn, uint8_t offset,
	uint8_t len, uint32_t value)
{
	outl(0xCF8, PCI_ADDRESS(bus, devfn, offset));

	switch(len) {
	case 1: outb(0xCFC + (offset & 3), (uint8_t) value); break;
	case 2: outw(0xCFC + (offset & 2), (uint16_t) value); break;
	case 4: outl(0xCFC, (uint32_t) value); break;
	}
}

static uint32_t pci_read_conf2(uint8_t bus, uint8_t devfn, uint8_t offset,
	uint8_t len)
{
	uint32_t res = 0xFFFF;

	if(devfn & 0x80) {
		return res;
	}

	outb(0xCF8, PCI_FUNC(devfn));
	outb(0xCFA, bus);

	switch(len) {
	case 1: res = inb(PCI_IOADDR(devfn, offset)); break;
	case 2: res = inw(PCI_IOADDR(devfn, offset)); break;
	case 4: res = inl(PCI_IOADDR(devfn, offset)); break;
	}

	outb(0xCF8, 0);

	return res;
}

static void pci_write_conf2(uint8_t bus, uint8_t devfn, uint8_t offset,
	uint8_t len, uint32_t value)
{
	outb(0xCF8, PCI_FUNC(devfn));
	outb(0xCFA, bus);

	switch(len) {
	case 1: outb(PCI_IOADDR(devfn, offset), value); break;
	case 2: outw(PCI_IOADDR(devfn, offset), value); break;
	case 4: outl(PCI_IOADDR(devfn, offset), value); break;
	}

	outb(0xCF8, 0);
}

static pci_access_t pci_conf1_access = {
	.read = pci_read_conf1,
	.write = pci_write_conf1
};

static pci_access_t pci_conf2_access = {
	.read = pci_read_conf2,
	.write = pci_write_conf2
};

void arch_pci_write(uint8_t bus, uint8_t devfn, uint8_t offset, int8_t len,
	uint32_t value)
{
	pci_access->write(bus, devfn, offset, len, value);
}

uint32_t arch_pci_read(uint8_t bus, uint8_t devfn, uint8_t offset,
	uint8_t len)
{
	return pci_access->read(bus, devfn, offset, len);
}

int __init arch_pci_init(void) {
	uint32_t tmp, val;

	/*
	 * Check for configuration type 1
	 */
	outb(0xCFB, 0x1);
	tmp = inl(0xCF8);
	outl(0xCF8, 0x80000000);
	val = inl(0xCF8);
	outl(0xCF8, tmp);

	if(val == 0x80000000) {
		pci_access = &pci_conf1_access;
		return 0;
	}

	/*
	 * Check for configuration type 2.
	 */
	outb(0xCFB, 0x00);
	outb(0xCF8, 0x00);
	outb(0xCFA, 0x00);
	if(inb(0xCF8) == 0x00 && inb(0xCFB) == 0x00) {
		pci_access = &pci_conf2_access;
		return 0;
	}
	
	return -ENODEV;
}

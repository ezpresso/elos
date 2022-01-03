#ifndef ARCH_PCI_H
#define ARCH_PCI_H

int arch_pci_init(void);
void arch_pci_write(uint8_t bus, uint8_t devfn, uint8_t offset, uint8_t len,
	uint32_t value);
uint32_t arch_pci_read(uint8_t bus, uint8_t devfn, uint8_t offset, uint8_t len);

#endif

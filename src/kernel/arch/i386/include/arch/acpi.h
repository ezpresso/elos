#ifndef ARCH_ACPI_H
#define ARCH_ACPI_H

#include <kern/multiboot.h>

static inline void *acpi_get_rsdp(void) {
	/*
	 * The bootloader is very nice to the kernel. Thank you bootloader!
	 */
	return multiboot_acpi_rsdp();
}

#endif
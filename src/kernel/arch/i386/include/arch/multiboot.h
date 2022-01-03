#ifndef ARCH_MULTIBOOT_H
#define ARCH_MULTIBOOT_H

#define BOOT_ARCH 	MULTIBOOT_ARCHITECTURE_I386
#define BOOT_IREQ 	{ MULTIBOOT_TAG_TYPE_MMAP }
#define BOOT_IREQ_NUM	1
#define BOOT_ENTRY	(uint32_t)entry

extern void entry(void);

#endif
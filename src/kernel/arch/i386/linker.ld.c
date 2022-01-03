#include <vm/layout.h>

#undef i386
OUTPUT_FORMAT(elf32-i386)
ENTRY(entry)

SECTIONS {
	. = KERNEL_LOAD_ADDR;

	/* TODO this could technically be freed after init. */
	.boot KERNEL_LOAD_ADDR : {
		*(.multiboot)
		*(.boot_stack)
		*(.boot_text)
	}

	.ap_entry 0x7000 : AT(LOADADDR(.boot) + SIZEOF(.boot)) {
		ap_entry_start = LOADADDR(.ap_entry);
		*(.ap_entry)
		ap_entry_end = LOADADDR(.ap_entry) + SIZEOF(.ap_entry);
	}

	. = LOADADDR(.ap_entry) + SIZEOF(.ap_entry);
	. += KERNEL_VM_BASE;

	.text ALIGN (0x1000) : AT(ADDR(.text) - KERNEL_VM_BASE) {
		kern_text_start = .;
		*(.text)
		*(.rodata*)
		*(.debug*)
		kern_text_end = .;
	}

	.data ALIGN (0x1000) : AT(ADDR(.data) - KERNEL_VM_BASE) {
		*(.data)
	}

	.bss ALIGN (0x1000) : AT(ADDR(.bss) - KERNEL_VM_BASE) {
		bss = .;
		*(COMMON)
		*(.bss)
	}

	.kernel_args ALIGN (0x1000) : AT(ADDR(.kernel_args) - KERNEL_VM_BASE) {
		karg_handlers = .;
		*(.kargs)
		__karg_handlers_end = .;
	}

	. = ALIGN(0x1000);
	init_start_addr = .;

	.initcall.init : AT(ADDR(.initcall.init) - KERNEL_VM_BASE) {
		__initcall0_start = .;
		*(.initcall0.init)
		__initcall1_start = .;
		*(.initcall1.init)
		__initcall2_start = .;
		*(.initcall2.init)
		__initcall3_start = .;
		*(.initcall3.init)
		__initcall_end = .;
	}

	/* TODO should be in the text area */
	.initcode ALIGN (0x1000) : AT(ADDR(.initcode) - KERNEL_VM_BASE) {
		*(.initcode)
	}

	.initdata ALIGN (0x1000) : AT(ADDR(.initdata) - KERNEL_VM_BASE) {
		*(.initdata)
	}

	. = ALIGN(0x1000);
	init_end_addr = .;
	end = .;

	/DISCARD/ :
	{
		*(.comment)
		*(.eh_frame)
	}
}

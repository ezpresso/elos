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
#include <kern/multiboot.h>
#include <kern/cpu.h>
#include <kern/proc.h>
#include <kern/main.h>
#include <vm/mmu.h>
#include <vm/vm.h>

void __noreturn asmlinkage arch_main(uintptr_t mboot_phys,
	uint32_t mboot_magic)
{
	assert(mboot_magic == MULTIBOOT2_BOOTLOADER_MAGIC);

	/*
	 * Initialize segments (stack smashing protector expects %gs to be
	 * setup)
	 */
	arch_cpu_init(&boot_cpu.arch);
	mmu_init();

	/*
	 * sched_exit_free uses the kstack to store the async-call structure,
	 * when the init thread exits.
	 */
	extern uintptr_t boot_stack_ptr;
	boot_thread.kstack = (void *)&boot_stack_ptr + KERNEL_VM_BASE;

	multiboot_init(mboot_phys);
	kern_main();
}

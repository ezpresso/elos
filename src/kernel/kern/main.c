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
#include <kern/cpu.h>
#include <kern/sched.h>
#include <kern/proc.h>
#include <kern/futex.h>
#include <kern/exec.h>
#include <kern/symbol.h>
#include <kern/async.h>
#include <kern/mp.h>
#include <kern/timer.h>
#include <kern/time.h>
#include <vfs/vfs.h>
#include <vfs/proc.h>
#include <vfs/file.h>
#include <vm/vm.h>
#include <vm/pageout.h>
#include <vm/reclaim.h>
#include <block/block.h>
#include <device/device.h>
#include <sys/unistd.h>
#include <config.h>

/**
 * @brief Initialize the first user process.
 */
void user_main(void) {
	const char *args[] = { "/bin/init", NULL };
	const char *env[] = { NULL };
	file_t *file;
	int err, res;

	/*
	 * Load the init process image.
	 */
	err = kern_execve("/bin/init", args, env, KERNELSPACE);
	if(err) {
		kpanic("[init] could not execute /bin/init: %d", err);
	}

	/*
	 * Give the init process a way to output information.
	 */
	err = kern_open("/dev/console", O_WRONLY, 0, &file);
	if(err) {
		kpanic("[init] could not open /dev/console: %d", err);
	}

	res = fddup(file, STDOUT_FILENO);
	assert(res == STDOUT_FILENO);
	res = fddup(file, STDERR_FILENO);
	assert(res == STDERR_FILENO);
	file_unref(file);
}

/**
 * @brief Entry point for non-boot processors.
 */
#if CONFIGURED(MP)
void __noreturn ap_main(void) {
	cpu_intr_set(true);
	vm_init_cpu();
	mp_ap_wait();
	kern_exit(0);
	notreached();
}
#endif

/* TODO remove */
#include "../drivers/acpi/include/acpi/table.h"
#include <arch/acpi.h>
#include <lib/checksum.h>

/**
 * @brief Entry point for bootstrap processor
 */
void __init __noreturn kern_main(void) {
	assert(cur_cpu() == &boot_cpu);
	kprintf("ELOS kernel v" KERN_VERSION " from " BUILD_DATE "\n");

	/*
	 * The stack trace uses kernel symbols and it is very
	 * convenient to see function names in a stack trace
	 * very early on.
	 */
	init_symbol();

	/*
	 * Startup the memory manager.
	 */
	kprintf("[kmain] vm init\n");
	init_vm();

	/*
	 * Initialize early stuff.
	 */
	kprintf("[kmain] early initcall\n");
	init_level(INIT_EARLY);

	kprintf("[kmain] device early init\n");
	init_device_early();

	/*
	 * Interrupt controller(s) and other stuff are setup,
	 * which means it's safe to enable interrupts now.
	 */
	cpu_intr_set(true);

	/*
	 * Initialize system time stuff very early on.
	 */
	kprintf("[kmain] time init\n");
	init_timer();
	kprintf("[kmain] timekeep init\n");
	init_timekeep();

	/*
	 * Start up multithreading.
	 */
	kprintf("[kmain] futex init\n");
	init_futex();
	kprintf("[kmain] thread init\n");
	init_thread();
	kprintf("[kmain] proc init\n");
	init_proc();
	kprintf("[kmain] sched init\n");
	init_sched();

	/*
	 * Initialize the virtual file system.
	 */
	kprintf("[kmain] vfs init\n");
	init_vfs();

	/* TODO remove */
#if 0
	extern void nvidia_test(void);
	nvidia_test();
	for(;;) ;
#endif

	/*
	 * Initialize the block device manager.
	 */
	kprintf("[kmain] block init\n");
	init_block();

	/*
	 * Initialize all of the devices in the system.
	 */
	kprintf("[kmain] device init\n");
	init_device();

	/*
	 * INIT_FS and INIT_DEV will eventually have
	 * to be renamed OR completely removed
	 */
	kprintf("[kmain] init level fs\n");
	init_level(INIT_FS);
	kprintf("[kmain] init level dev\n");
	init_level(INIT_DEV);
	kprintf("[kmain] init level late\n");
	init_level(INIT_LATE);

	kprintf("[kmain] async init\n");
	init_async();

	kprintf("[kmain] pageout init\n");
	vm_pageout_launch();
	kprintf("[kmain] reclaim init\n");
	vm_reclaim_launch();

	kprintf("[kmain] mp init\n");
	init_mp();

	kprintf("[kmain] launching /bin/init\n");
	proc_spawn_init();

	/*
	 * Exit the boot thread.
	 */
	kprintf("[kmain] done\n");
	kern_exit(0);
	notreached();
}

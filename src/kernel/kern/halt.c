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
#include <kern/atomic.h>
#include <kern/halt.h>
#include <kern/proc.h>

#if 0
static bool shutdown = false;

static void shutdown_default() {
	cpu_intr_set(false);
	kprintf("the system has stopped\n");
	/* TODO 'hlt'-instruction */
	for(;;) ;
}

static int deinit(void) {
	if(atomic_xchg_relaxed(&shutdown, true) == true) {
		return -EINTR;
	}

	/*
	 * Halt all threads and processes.
	 */
	proc_shutdown();

	/*
	 * Halt other cpus.
	 */
	cpu_shutdown();

	/*
	 * Flush vfs caches.
	 */
	vfs_shutdown();

	/*
	 * Flush blkdev caches.
	 */
	blk_shutdown();

	/*
	 * Bye bye.
	 */
	return 0;
}

int reboot(void) {
	int err;

	err = deinit();
	if(err) {
		return -EINTR;
	}

	/*
	 * Hand over the request to mainboard driver.
	 */
	if(/* mboard_reboot() < 0 */ true) {
		shutdown_default();
	}
}

int shutdown(void) {
	int err;

	err = deinit();
	if(err) {
		return -EINTR;
	}

	/* Hand over the request to mainboard driver */
	if(/* mboard_shutdown() < 0 */ true) {
		shutdown_default();
	}
}
#endif

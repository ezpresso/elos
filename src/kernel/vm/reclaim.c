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
#include <kern/proc.h>
#include <kern/init.h>
#include <kern/time.h>
#include <kern/sched.h>
#include <vm/reclaim.h>
#include <vm/pressure.h>

#define VM_RECLAIM_BREAK 10

static DEFINE_LIST(vm_reclaim_list);
static sync_t vm_reclaim_lock = SYNC_INIT(MUTEX);
static thread_t *vm_reclaim_thread;

void vm_reclaim_add(vm_reclaim_t *reclaim) {
	list_node_init(reclaim, &reclaim->node);
	sync_scope_acquire(&vm_reclaim_lock);
	list_append(&vm_reclaim_list, &reclaim->node);
}

void vm_reclaim_rem(vm_reclaim_t *reclaim) {
	synchronized(&vm_reclaim_lock) {
		list_append(&vm_reclaim_list, &reclaim->node);
	}

	list_node_destroy(&reclaim->node);
}

static int vm_reclaim_loop(__unused void *arg) {
	vm_reclaim_t *reclaim;

	while(true) {
		/*
		 * Don't start reclaiming memory, if enough free memory
		 * is available.
		 */
		vm_pressure_wait(VM_PR_KERN | VM_PR_PHYS, VM_PR_MODERATE);

		synchronized(&vm_reclaim_lock) {
			reclaim = list_pop_front(&vm_reclaim_list);
			if(reclaim) {
				list_append(&vm_reclaim_list, &reclaim->node);

				if(reclaim->func()) {
#if 1
					kprintf("[reclaim] freed an object "
						"from \"%s\"\n", reclaim->name);
#endif
				}
			}
		}

		if(vm_pressure(VM_PR_KERN | VM_PR_PHYS) <= VM_PR_MODERATE) {
			/*
			 * If the pressure is moderate, do not reclaim
			 * everything. Do this by sleeping a short amount
			 * of time.
			 */
			msleep(VM_RECLAIM_BREAK);
		}
	}

	notreached();
}

void __init vm_reclaim_launch(void) {
	vm_reclaim_t *reclaim;

	section_foreach(reclaim, VM_RECLAIM_SECTION) {
		vm_reclaim_add(reclaim);
	}

	vm_reclaim_thread = kthread_spawn_prio(vm_reclaim_loop, NULL,
		SCHED_KERNEL);
}

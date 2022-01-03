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
#include <kern/proc.h>
#include <kern/init.h>
#include <kern/sched.h>
#include <kern/futex.h>
#include <kern/sync.h>
#include <block/block.h>
#include <vm/slab.h>

static thread_t *blk_event_thread;
static DEFINE_VM_SLAB(blk_event_slab, sizeof(blk_event_t), 0);
static DEFINE_LIST(blk_event_queue);
static sync_t blk_event_lock = SYNC_INIT(SPINLOCK);
static size_t blk_event_count = 0;

void blk_event_create(blk_event_t *event, blk_callback_t callback, void *arg) {
	list_node_init(event, &event->node);
	event->callback = callback;
	event->arg = arg;
}

void blk_event_destroy(blk_event_t *event) {
	list_node_destroy(&event->node);
}

blk_event_t *blk_event_alloc(blk_callback_t callback, void *arg) {
	blk_event_t *event = vm_slab_alloc(&blk_event_slab, VM_WAIT);
	blk_event_create(event, callback, arg);
	return event;
}

void blk_event_free(blk_event_t *event) {
	blk_event_destroy(event);
	vm_slab_free(&blk_event_slab, event);
}

void blk_event_add(blk_event_t *event) {
	synchronized(&blk_event_lock) {
		list_append(&blk_event_queue, &event->node);
	}

	/*
	 * Wakeup the event handling thread.
	 */
	atomic_inc_relaxed(&blk_event_count);
	kern_wake(&blk_event_count, 1, 0);
}

static int blk_event_loop(__unused void *arg) {
	sync_acquire(&blk_event_lock);

	while(true) {
		blk_event_t *event;

		/*
		 * Wait for any pending events, which need to be handled.
		 */
		while(atomic_load_relaxed(&blk_event_count) == 0) {
			sync_release(&blk_event_lock);
			kern_wait(&blk_event_count, 0U, 0);
			sync_acquire(&blk_event_lock);
		}

		/*
		 * Handle the events.
		 */
		while((event = list_pop_front(&blk_event_queue))) {
			sync_release(&blk_event_lock);

			atomic_dec_relaxed(&blk_event_count);
			rdlocked(&blk_lock) {
				event->callback(event->arg);
			}

			sync_acquire(&blk_event_lock);
		}
	}

	notreached();
}

void __init blk_event_init(void) {
	blk_event_thread = kthread_spawn_prio(blk_event_loop, NULL, SCHED_IO);
}

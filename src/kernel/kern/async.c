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
#include <kern/async.h>
#include <kern/sync.h>
#include <kern/init.h>
#include <kern/wait.h>
#include <kern/proc.h>
#include <kern/sched.h>

static sync_t async_lock = SYNC_INIT(SPINLOCK);
static DEFINE_LIST(async_calls);
static DEFINE_WAITQUEUE(async_wq);

void async_call(async_t *call, async_func_t func, void *arg) {
	list_node_init(call, &call->node);
	call->func = func;
	call->arg = arg;

	synchronized(&async_lock) {
		list_append(&async_calls, &call->node);
	}

	wakeup(&async_wq, SCHED_KERNEL);
}

static inline async_t *async_next(void) {
	sync_scope_acquire(&async_lock);
	return list_pop_front(&async_calls);
}

static inline int async_thread(__unused void *arg) {
	waiter_t wait;
	async_t *async;
	int err;

	waiter_init(&wait);

	for(;;) {
		wait_prep(&async_wq, &wait);

		while((async = async_next())) {
			list_node_destroy(&async->node);
			async->func(async->arg);
		}

		err = wait_sleep(&async_wq, &wait, WAIT_INTERRUPTABLE);
		if(err) {
			kpanic("[async] thread killed");
		}
	}

	notreached();
}

void __init init_async(void) {
	kthread_spawn(async_thread, NULL);
}

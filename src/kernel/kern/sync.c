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
#include <kern/sync.h>
#include <kern/sched.h>
#include <kern/proc.h>
#include <kern/symbol.h>
#include <kern/critical.h>
#include <kern/sched.h>
#include <kern/futex.h>
#include <kern/cpu.h>

static void sync_check(sync_t *sync) {
	magic_check(&sync->magic, SYNC_MAGIC);
	if(sync->type != SYNC_SPINLOCK && sync->type != SYNC_MUTEX) {
		kpanic("[sync] unknown lock type: %d", sync->type);
	}
}

void sync_init(sync_t *sync, int type) {
	DEVEL_SET(sync->file, NULL);
	DEVEL_SET(sync->line, -1);
	magic_init(&sync->magic, SYNC_MAGIC);

	sync->thread = NULL;
	sync->type = type;
	sync->waiting = 0;
}
export(sync_init);

bool __sync_assert(sync_t *sync) {
	sync_check(sync);
	return atomic_load_relaxed(&sync->thread) == cur_thread();
}
export(__sync_assert);

bool __sync_trylock(sync_t *sync, const char *file, int line) {
	thread_t *thr = cur_thread();

	sync_check(sync);
	if(sync->type == SYNC_SPINLOCK) {
		critical_enter();
	} else {
		thread_numlock_inc();
	}

	if(atomic_cmpxchg(&sync->thread, NULL, thr) == false) {
		if(sync->type == SYNC_SPINLOCK) {
			critical_leave();
		} else {
			thread_numlock_dec();
		}

		return false;
	} else {
		DEVEL_SET(sync->file, file);
		DEVEL_SET(sync->line, line);
		return true;
	}
}
export(__sync_trylock);

/*
 * TODO remove
 */
#include <kern/mp.h>

void __sync_acquire(sync_t *sync, const char *file, int line) {
	thread_t *lock, *thread = cur_thread();

	sync_check(sync);

	/*
	 * When the sync is able to sleep, the processor should not be inside
	 * a critical section.
	 */
	if(sync->type == SYNC_MUTEX && critsect_p()) {
		kpanic("[sync] sleep during critical section at %s:%d",
			file, line);
	}

	if(sync->type == SYNC_SPINLOCK) {
		critical_enter();
	} else {
		thread_numlock_inc();
	}

	while((lock = atomic_cmpxchg_val(&sync->thread, NULL, thread)) !=
		NULL)
	{
		if(lock == thread) {
			kpanic("[sync] double lock: locked at %s:%d, "
				"locking at %s:%d\n", sync->file,
				sync->line, file, line);
		}

		if(sync->type == SYNC_MUTEX) {
			if(!bsp_p() && !ipi_enabled) {
				kpanic("[sync] locked at: %s:%d; locking at "
					"%s:%d (0x%p)\n", sync->file,
					sync->line, file, line, lock);
			}

#if 0
			kprintf("[sync] locked at: %s:%d; locking at %s:%d (%d)\n",
				sync->file, sync->line, file, line, cur_cpu()->id);
#endif

			thread_numlock_dec();
			atomic_inc_relaxed(&sync->waiting);
			kern_wait(&sync->thread, lock, 0);
			atomic_dec_relaxed(&sync->waiting);
			thread_numlock_inc();
		} else {
			critical_leave();
			cpu_relax();
			critical_enter();
		}
	}

	DEVEL_SET(sync->file, file);
	DEVEL_SET(sync->line, line);
}
export(__sync_acquire);

void sync_release(sync_t *sync) {
	sync_check(sync);
	DEVEL_SET(sync->file, NULL);
	DEVEL_SET(sync->line, -1);

	/*
	 * Cannot check for xchg(thread, NULL) == cur_thread(), because
	 * scheduler exchanges sched->thread with the new thread on schedule()
	 * while HODLing the sched lock.
	 * TODO maybe move the scheduler over to spinlock_t?
	 */
	if(atomic_xchg(&sync->thread, NULL) == NULL) {
		kpanic("[sync] unlocking a lock not locked");
	}

	if(sync->type == SYNC_SPINLOCK) {
		critical_leave();
	} else {
		thread_numlock_dec();
		if(atomic_load_relaxed(&sync->waiting)) {
			kern_wake(&sync->thread, 1, 0);
		}
	}
}
export(sync_release);

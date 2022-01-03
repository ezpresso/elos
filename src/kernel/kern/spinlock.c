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
#include <kern/spinlock.h>
#include <kern/atomic.h>
#include <kern/critical.h>
#include <kern/cpu.h>

bool spin_try_lock(spinlock_t *lock) {
	uint8_t val;
	
	critical_enter();
	val = atomic_xchg_relaxed(lock, SPIN_LOCKED);
	if(val == SPIN_LOCKED) {
		critical_leave();
		return false;
	} else {
		return true;
	}
}

bool spin_locked(spinlock_t *lock) {
	return atomic_load_relaxed(lock) == SPIN_LOCKED;
}

void spin_lock(spinlock_t *lock) {
	critical_enter();

	while(atomic_xchg_relaxed(lock, SPIN_LOCKED) == SPIN_LOCKED) {
		critical_leave();
		cpu_relax();
		critical_enter();
	}
}

void __spin_unlock(spinlock_t *lock, char *file, int line) {
	if(!atomic_load_relaxed(lock)) {
		kpanic("[spinlock] unlocking unlocked spinlock %s:%d", file,
			line);
	}

	atomic_store_relaxed(lock, SPIN_UNLOCKED);
	critical_leave();
}

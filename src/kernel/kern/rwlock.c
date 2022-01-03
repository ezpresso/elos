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
#include <kern/rwlock.h>
#include <kern/rwspin.h>
#include <kern/atomic.h>
#include <kern/futex.h>
#include <kern/critical.h>
#include <kern/cpu.h>
#include <sys/limits.h>
#include <arch/barrier.h>

void rwlock_init(rwlock_t *lock) {
	*lock = RWLOCK_INIT;
}

void wrlock(rwlock_t *lock) {
	assert_not_critsect("[rwlock] called wrlock during critical section");

	spin_lock(&lock->lock);

	/*
	 * Wait until there are no writers and no
	 * readers.
	 */
	while(lock->rdnum || lock->wrlock) {
		uint32_t val = lock->wfutex;

		lock->wrwait++;
		spin_unlock(&lock->lock);
		kern_wait(&lock->wfutex, val, 0);
		spin_lock(&lock->lock);	
		lock->wrwait--;
	}

	/*
	 * The read futex and the write futex
	 * both depend on wrlock -> wrlock
	 * is stored in both futex words.
	 */
	lock->wrlock = 1;
	lock->wrlock2 = 1;

	spin_unlock(&lock->lock);
}

void rdlock(rwlock_t *lock) {
	assert_not_critsect("[rwlock] called wrlock during critical section");

 	spin_lock(&lock->lock);

 	/*
	 * Wait until there is no writer.
 	 * Futhermore do not acquire
 	 * the lock if writers are waiting.
 	 */
	while(lock->wrlock || lock->wrwait) {
		uint32_t val = lock->rfutex;

		lock->rdwait++;
		spin_unlock(&lock->lock);
		kern_wait(&lock->rfutex, val, 0);
		spin_lock(&lock->lock);	
		lock->rdwait--;
	}

	lock->rdnum++;
	spin_unlock(&lock->lock);
}

void rwunlock(rwlock_t *lock) {
	int num = 0;

	spin_lock(&lock->lock);
	if(lock->rdnum) {
		if(--lock->rdnum != 0) {
			spin_unlock(&lock->lock);

			/*
			 * There are still some readers -> waking
			 * up any waiting writers/readers is useless.
			 */
			return;
		}
	} else {
		kassert(lock->wrlock, "[rwlock] unlocking unlocked rwlock");
		lock->wrlock = 0;
		lock->wrlock2 = 0;
	}

	spin_unlock(&lock->lock);

	if(lock->wrwait) {
		/*
		 * Wakeup 1 writer.
		 */
		num = kern_wake(&lock->wfutex, 1, 0);
	}

	/*
	 * If no writer has been woken up and there
	 * are waiting readers, wake all of them.
	 */
	if(num == 0 && lock->rdwait) {
		/*
		 * Wakeup the readers.
		 */
		kern_wake(&lock->rfutex, INT_MAX, 0);
	}
}

void rwlock_cleanup(rwlock_t **l) {
	if(*l) {
		rwunlock(*l);
	}
}

static uint16_t rwlock_spin_ticket(rwlock_spin_t *l) {
	return atomic_inc(&l->ticket);
}

void rwlock_spin_init(rwlock_spin_t *l) {
	*l = RWLOCK_SPIN_INIT;
}

void rdlock_spin(rwlock_spin_t *l) {
	uint16_t ticket = rwlock_spin_ticket(l);

	for(;;) {
		critical_enter();
		if(atomic_load(&l->read) == ticket) {
			break;
		}
		critical_leave();
		cpu_relax();
	}

	atomic_inc(&l->read);
}

void rdunlock_spin(rwlock_spin_t *l) {
	assert_critsect("[rwlock] rdunlock: caller left the critical section");
	atomic_inc(&l->write);
	critical_leave();
}

void wrlock_spin(rwlock_spin_t *l) {
	uint16_t ticket = rwlock_spin_ticket(l);

	for(;;) {
		critical_enter();
		if(atomic_load(&l->write) == ticket) {
			break;
		}
		critical_leave();
		cpu_relax();
	}
}

void wrunlock_spin(rwlock_spin_t *lock) {
	rwlock_spin_t l = *lock;

	assert_critsect("[rwlock] wrunlock: caller left the critical section");

	barrier();
	l.write++;
	l.read++;

	atomic_store(&lock->rw, l.rw);
	critical_leave();
}

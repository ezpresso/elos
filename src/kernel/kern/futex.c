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
#include <kern/wait.h>
#include <kern/futex.h>
#include <kern/init.h>
#include <kern/user.h>
#include <kern/atomic.h>
#include <kern/sched.h>
#include <lib/list-locked.h>
#include <lib/string.h>
#include <vm/vas.h>
#include <vm/object.h>
#include <sys/futex.h>
#include <sys/pow2.h>

/*
 * TODO increae that sucker
 */
#define FHT_SIZE 	1024 /* Must be a power of 2 */
#define FHT_MASK	(FHT_SIZE - 1)
#define FHT_SHIFT	8
#define FHT_HASH(x, y)	((((x) >> FHT_SHIFT) ^ (y)) & FHT_MASK)
#define FUTEX_SZ_MAX	sizeof(uint64_t)

typedef struct futex_addr {
	union {
		vm_object_t *object;
		vm_vas_t *vas;
		void *ptr;
	};

	uint64_t addr;
	bool shared;
} futex_addr_t;

typedef struct futex_wait {
	waiter_t wait;
	futex_addr_t addr;
} futex_wait_t;

static waitqueue_t futex_khash[FHT_SIZE];
static waitqueue_t futex_uhash[FHT_SIZE];

static futex_wait_t *waiter_to_futex(waiter_t *wait) {
	return container_of(wait, futex_wait_t, wait);
}

/**
 * @brief Get the waitqueue on which the caller should sleep.
 */
static inline waitqueue_t *futex_waitq(futex_addr_t *addr, int flags) {
	size_t hash = FHT_HASH(addr->addr, (uintptr_t)addr->ptr);
	if(flags & KWAIT_USR) {
		return &futex_uhash[hash];
	} else {
		return &futex_khash[hash];
	}
}

/**
 * @brief Initialize a futex address.
 */
static int futex_addr(void *ptr, int flags, futex_addr_t *faddr) {
	vm_vaddr_t addr = (vm_vaddr_t) ptr;
	vm_vas_t *vas = vm_vas_current;

	/*
	 * If a region, where a thread is currently waiting for a
	 * futex wake, is unmapped, the thread cannot be woken
	 * up again. Furthermore some problems may occur
	 * if the object of a shared futex is split or the object
	 * shrinks.
	 * These cases are simply ignored here.
	 */
	if((flags & (KWAIT_PRIV | KWAIT_USR)) == KWAIT_USR) {
		vm_map_t *map;
		int err;

		err = vm_vas_lookup(vas, addr, &map);
		if(err) {
			return -EFAULT;
		}

		if(VM_MAP_SHARED_P(map->flags)) {
			faddr->object = vm_object_ref(map->object);
			faddr->addr = map->offset + (addr - vm_map_addr(map));
			vm_vas_lookup_done(map);
			faddr->shared = true;
			return 0;
		}

		vm_vas_lookup_done(map);
	}

	if((flags & KWAIT_USR) == 0) {
		vas = &vm_kern_vas;
	}

	faddr->shared = false;
	faddr->vas = vas;
	faddr->addr = addr;

	return 0;
}

/**
 * @brief Compare two futex addresses.
 *
 * @retval true  The addresses are equal.
 * @retval false The addresses are not equal.
 */
static inline bool futex_addr_cmp(futex_addr_t *a1, futex_addr_t *a2) {
	return a1->shared == a2->shared && a1->ptr == a2->ptr &&
		a1->addr == a2->addr;
}

/**
 * @brief Cleanup a futex address retunred by @p futex_addr.
 */
static void futex_addr_done(futex_addr_t *faddr) {
	if(faddr->shared) {
		vm_object_unref(faddr->object);
	}
}

int __kern_wait(void *addr, size_t val_size, void *val, int flags,
	struct timespec *timeout)
{
	uint8_t buf[FUTEX_SZ_MAX];
	futex_wait_t wait;
	waitqueue_t *waitq;
	int err;

	assert(pow2_p(val_size));
	assert(val_size <= FUTEX_SZ_MAX);

	err = futex_addr(addr, flags, &wait.addr);
	if(err) {
		return err;
	}

	waitq = futex_waitq(&wait.addr, flags);

	/*
	 * Register or waiter on the waitqueue.
	 */
	wait_init_prep(waitq, &wait.wait);

	/*
	 * Check if _addr_ still contains the contents _val_.
	 */
	if(flags & KWAIT_USR) {
		err = copyin_atomic(buf, addr, val_size);
	} else {
		atomic_loadn(buf, addr, val_size);
	}

	if(err == 0) {
		/*
		 * The contents of _val_ changed, so abort the wait.
		 */
		if(memcmp(val, buf, val_size)) {
			err = -EAGAIN;
		}
	}

	if(err) {
		wait_abort(waitq, &wait.wait);
		goto out;
	}

	/*
	 * Sleep until a kern_wake() or an interrupt wakes us up again.
	 */
	err = wait_sleep_timeout(waitq, &wait.wait,
		(flags & KWAIT_INTR) ? WAIT_INTERRUPTABLE : 0,
		timeout);
	/*
	 * TODO
	 */
	if(err == -ERESTART) {
		err = -EINTR;
	}

out:
	futex_addr_done(&wait.addr);
	waiter_destroy(&wait.wait);
	return err;
}

int kern_wake(void *addr, int num, int flags) {
	waitqueue_t *waitq;
	futex_addr_t faddr;
	futex_wait_t *wait;
	int err, i = 0;
	waiter_t *cur;

	if(num < 0) {
		return 0;
	}

	err = futex_addr(addr, flags, &faddr);
	if(err) {
		return err;
	}

	/*
	 * Wakeup a number of sleeping threads.
	 */
	waitq = futex_waitq(&faddr, flags);
	waitq_foreach(cur, waitq) {
		wait = waiter_to_futex(cur);

		if(futex_addr_cmp(&faddr, &wait->addr)) {
			wakeup_waiter(waitq, cur, SCHED_NORMAL);
			if(++i >= num) {
				break;
			}
		}
	}

	futex_addr_done(&faddr);
	return i;
}

int sys_futex(int *uaddr, int op, int val, const struct timespec *utimeout,
	int *uaddr2, int val3)
{
	int err, flags = KWAIT_USR | KWAIT_PRIV;
	struct timespec ts, *timeout;

	if(utimeout != NULL) {
		err = copyin_ts(&ts, utimeout);
		if(err) {
			return err;
		}

		timeout = &ts;
	} else {
		timeout = NULL;
	}

	if(F_ISSET(op, FUTEX_PRIVATE)) {
		F_SET(flags, KWAIT_PRIV);
	}

	(void) uaddr2;
	(void) val3;

	switch(op & ~(FUTEX_PRIVATE | FUTEX_CLOCK_REALTIME)) {
	case FUTEX_WAIT:
		/*
		 * Futexes are 32bits on all platforms, even on 64bit machines.
		 * Thus cast the pointer into an u32-pointer so that the
		 * kern_wait_timeout macro will interpret this futex as a 32-bit
		 * futex. Actually the kernel can use 8, 16, 32 and 64 bit
		 * futexes for internal kernel stuff. However the name futex
		 * seems wrong in this case...
		 */
		return kern_wait_timeout((uint32_t *)uaddr, (uint32_t)val, flags,
			timeout);
	case FUTEX_WAKE:
		return kern_wake(uaddr, val, flags);
	case FUTEX_FD: /* FALLTHROUGH */
	case FUTEX_REQUEUE: /* FALLTHROUGH */
	case FUTEX_CMP_REQUEUE: /* FALLTHROUGH */
	case FUTEX_WAKE_OP: /* FALLTHROUGH */
	case FUTEX_LOCK_PI: /* FALLTHROUGH */
	case FUTEX_UNLOCK_PI: /* FALLTHROUGH */
	case FUTEX_TRYLOCK_PI: /* FALLTHROUGH */
	case FUTEX_WAIT_BITSET: /* FALLTHROUGH */
		kprintf("[futex] warning unsupported op: %d", op);
		return -ENOTSUP;
	default:
		return -EINVAL;
	}
}

static void futex_hash_init(waitqueue_t *hash) {
	for(size_t i = 0; i < FHT_SIZE; i++) {
		waitqueue_init(&hash[i]);
	}
}

void init_futex(void) {
	futex_hash_init(futex_khash);
	futex_hash_init(futex_uhash);
}

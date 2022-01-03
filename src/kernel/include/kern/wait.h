#ifndef KERN_WAIT_H
#define KERN_WAIT_H

#include <kern/sync.h>
#include <kern/time.h>
#include <lib/list.h>
#include <lib/list-locked.h>

enum sched_prio;

/* Example:
 *
 * INIT:
 * waiter_t waiter;
 * waiter_init(&waiter);
 *
 * WAIT:
 * sync_acuire(&x->lock);
 * while(x->value >= 5) {
 *  // Prepare the wait before unlocking x, to not miss any wakeup().
 * 	wait_prep(&x->wq, &waiter);
 *  syn_release(&x->lock);
 *
 *  // Wait until somebody wakes us up again.
 *  wait_sleep(&x->wq, &waiter, 0 or WAIT_INTERRUPTABLE);
 *  sync_acuire(&x->lock);
 * }
 *
 * x->value++;
 * syn_release(&x->lock);
 * waiter_destroy(&waiter);
 *
 * WAKEUP:
 * sync_acquire(&x->lock);
 * x->value--;
 * wakeup(&x->wq, SCHED_IO);
 * sync_release(&x->lock);
 */

/* flags for wait_sleep */
#define WAIT_INTERRUPTABLE (1 << 0)

#define DEFINE_WAITQUEUE(name) \
	waitqueue_t name = { \
		.lock = __SYNC_INIT(SPINLOCK), \
		.waiters = __LIST_FIELDS_INIT(&name.waiters), \
	}

typedef struct waitqueue {
	sync_t lock;
	list_t waiters;
} waitqueue_t;

typedef struct waiter {
	/*
	 * These fields are protected by waitqueue->lock.
	 */
	list_node_t node;
	uintptr_t thread_flags;
} waiter_t;

/**
 * @brief Iterate over a waitqueue.
 *
 *Iterate over a waitqueue. This locks the waitqueue while iterating.
 */
#define waitq_foreach(cur, wq) \
	foreach_locked(cur, &(wq)->waiters, &(wq)->lock)

/**
 * @brief Initialize a waitqueue.
 */
#define waitqueue_init(wq) ({			\
	sync_init(&(wq)->lock, SYNC_SPINLOCK);	\
	list_init(&(wq)->waiters);		\
})

/**
 * @brief Destroy a waitqueue.
 */
#define waitqueue_destroy(wq) ({	\
	sync_destroy(&(wq)->lock);	\
	list_destroy(&(wq)->waiters);	\
})

/**
 * @brief Initialize a waiter.
 */
static inline void waiter_init(waiter_t *wait) {
	list_node_init(wait, &wait->node);
}

/**
 * @brief Destroy a waiter.
 */
static inline void waiter_destroy(waiter_t *wait) {
	list_node_destroy(&wait->node);
}

/**
 * @brief Prepare to sleep on a waitqueue.
 *
 * Register this waiter on a waitqueue. The thread will not yet go to
 * sleep. Any wakeup after a call to this function will wake the thread
 * up even if wait_sleep was not called yet. In this case wait_sleep
 * will just return.
 */
void wait_prep(waitqueue_t *wq, waiter_t *wait);

/**
 * @see waiter_init
 * @ see wait_prep
 */
static inline void wait_init_prep(waitqueue_t *wq, waiter_t *wait) {
	waiter_init(wait);
	wait_prep(wq, wait);
}

/**
 * @brief Abort a wait operation.
 *
 * After calling wait_prep() a thread might decide not to sleep by
 * calling this function.
 */
void wait_abort(waitqueue_t *wq, waiter_t *wait);

/**
 * @brief Start sleeping.
 *
 * Sleep until a wakeup() wakes this thread up again or the sleep timed
 * out. The caller must have called wait_prep() prior to calling this
 * function. Remember that wait_prep() undos the effect of wait_prep().
 *
 * A quick note for kernel threads:
 * If a kernel thread sleeps, wait_sleep only returns -EINTR if it is killed.
 *
 * @retval 0			The thread was woken up again by wakeup().
 * @retval -ERESTART	The sleep was interrupted and the syscall should be
 *						restarted.
 * @retval -EINTR		The sleep was interrupted.
 * @retval -ETIMEDOUT	The sleep timed out.
 */
int wait_sleep_timeout(waitqueue_t *wq, waiter_t *wait, int flags,
	struct timespec *timeout);

/**
 * @see wait_sleep_timeout.
 * @retval 0
 * @retval -ERESTART
 * @retval -EINTR
 */
static inline int wait_sleep(waitqueue_t *wq, waiter_t *wait, int flags) {
	return wait_sleep_timeout(wq, wait, flags, NULL);
}

/**
 * @brief Wakeup everyone waiting on a waitqueue.
 */
void wakeup(waitqueue_t *wq, enum sched_prio prio);

/**
 * @brief Wakeup some threads waiting on a waitqueue.
 */
void wakeup_num(waitqueue_t *wq, enum sched_prio prio, size_t num);

/**
 * @brief Wakeup a specific waiter on a waitqueue.
 *
 * Wakeup a specific waiter on a waitqueue. The caller must hold the lock
 * of the waitqueue. Remember that waitq_foreach() automatically acquires
 * and releases the lock of the waitqueue.
 */
void wakeup_waiter(waitqueue_t *wq, waiter_t *wait, enum sched_prio prio);

#endif

#ifndef KERN_SCHED_H
#define KERN_SCHED_H

struct process;
struct thread;
struct cpu;

#define SCHEDQ_NLIST 32

typedef enum sched_prio {
	SCHED_INTR = 0,
	SCHED_IO,
	SCHED_LOCK, /* TODO should be removed */
	SCHED_KERNEL,
	SCHED_INPUT,
	SCHED_SIGNAL,
	SCHED_NORMAL,
	SCHED_PRIO_NUM,
} sched_prio_t;

static inline void sched_pin(void) {
	/* Threads currently not change the cpu, which means
	 * sched_pin is currently a no-op.
	 */
}

static inline void sched_unpin(void) {
}

/**
 * This function is called during critical_leave() to test whether we
 */
bool sched_need_resched(void);

/**
 * @brief	Check if there are any threads capable of running on
 *		the cpu's scheduler.
 */
bool sched_has_runnable(void);

/**
 * This is used by architecture code. The problem is that schedule()
 * does some work after the context switch and the scheduler assumes
 * that the context switch simply returns to the middle of the schedule
 * function. New cloned threads don't do that, they start at an address
 * specified in arch-code (e.g. fork_ret for i386). Architecture
 * code can call sched_postsched() to do the work which the schedule()
 * function normally does after the switch.
 */
void sched_postsched(void);

/**
 * Tell the scheduler that the current thread should not be chosen
 * again after the next call to schedule until the thread is woken
 * up by another thread or the thread is interrupted (e.g. due to a
 * signal), which is only possible if @p interruptable is true.
 * Do not use directly. Use the kern_wake/kern_wait in futex.h or
 * the kern/wait.h interface
 */
void sched_set_inactive(bool interruptable);

/**
 * @brief Wake up a thread.
 * @see sched_set_inactive
 *
 * Do not use directly.
 */
void sched_wakeup(struct thread *thread, sched_prio_t prio);

/**
 * @brief Interrupt a thread (software interrupt).
 *
 * This sends a software interrupt to a thread, waking up the thread
 * if necessary (except if @p interruptable was set to false in
 * sched_set_inactive ). The actual handling happens in thread_uret().
 */
void sched_interrupt(struct thread *thread, sched_prio_t prio, int intr,
	int flags);

/**
 * @retval 0 		The thread was not interrupted
 * @retval -EINTR	The thread was interrupted
 * @retval -ERESTART	The thread was interrupted and the syscall
 *			needs to be restarted.
 */
int sched_interrupted(void);

/**
 * @brief Check if any softare interrupts are pending.
 * Check if some software interrupts are pending for the current thread.
 * This also clears any pending software interrupts. Do not use directly.
 *
 * @return A bitwise mask of THREAD_KILL, THREAD_STOP, ...
 */
int sched_pending_intr(void);

/**
 * @brief Try to switch to another thread.
 *
 * If the current thread called sched_set_inactive and it has not yet
 * been woken up, the thread will not run again after the call to
 * schedule() until it is woken up.
 */
void schedule(void);

/**
 * @brief Schedule as soon as possible.
 *
 * This is used in a critical section, when a schedule is required. The
 * schedule cannot happen inside the section but on exit, schedule() will
 * be automatically called.
 */
void schedule_async(void);

/**
 * Check if the current thread needs to be preempted at the end of
 * an interrupt.
 */
void sched_intr_preempt(void);

/**
 * @brief Add a thread to the processor with the least amount of threads.
 */
void sched_add_thread(struct thread *thread);

void init_sched(void);
void sched_init_ap(struct cpu *cpu);

#endif

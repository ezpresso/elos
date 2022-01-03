#ifndef KERN_PROC_H
#define KERN_PROC_H

#include <kern/sync.h>
#include <kern/local.h>
#include <kern/wait.h>
#include <kern/atomic.h>
#include <lib/list.h>
#include <arch/thread.h>

/*
 * Lock order:
 * 1. proc_tree_lock
 * 2. proc_list_lock
 * 3. tty->lock
 * 4. session->lock
 * 5. pgrp->lock
 * 6. proc->lock
 */

#define THREAD_MAX		512
#define KTHREAD_TID		-1
#define UID_ROOT		0
#define PROC_FILES		50
#define INITPROC_PID		1

struct vm_vas;
struct trapframe;
struct rusage;
struct user_desc;
struct rlimit;

typedef struct session {
	sync_t lock;
	struct tty *tty;
	ref_t ref;
	pid_t id; /* (c) */
} session_t;

typedef struct pgrp {
	sync_t lock;

	struct session *session; /* (c) */
	list_node_t node;
	list_t members; /* protected by proc_tree_lock */
	pid_t id; /* (c) */
} pgrp_t;

/* Process flags */
/*
 * PROC_ZOMBIE indicates that no thread of the proc is alive anymore and wait4
 * can free the proc.
 */
#define PROC_ZOMBIE 	(1 << 0)
#define PROC_EXIT	(1 << 1) /* set to indicate the process wants to exit */
#define PROC_STOP	(1 << 2) /* used in conjunction with PROC_STATUS */
#define PROC_STATUS	(1 << 3) /* status changed */
/*
 * This means that if the last thread finally reaches thread_free,
 * proc_exit_final should be called (=> see thread_free).
 */
#define PROC_FREE	(1 << 4)
#define PROC_EXEC	(1 << 5)
#define PROC_ST 	(1 << 6) /* single thread mode */
#define PROC_AUTOREAP 	(1 << 7) /* SIGCHLG is beig ignored */

typedef struct proc_image {
	ref_t ref;
	char binary[];
} proc_image_t;

typedef struct proc {
	proc_image_t *image;

	/*
	 * TODO use kern_wait/wake interface for waitq and stop waitq
	 */
	waitqueue_t waitq;

	/*
	 * The threads of the process sleep
	 * on this queue, when the process is stopped.
	 */
	waitqueue_t stop_waitq;
	sync_t lock;

	/* protected by lock */
	int flags;
	int exit_code;
	int exit_sig;
	int stop_sig;

	/* single-thread mode */
	struct thread *st_thread;
	int st_mode;
	size_t st_waiting;

	list_t threads;
	list_node_t node_child;
	list_node_t node_pgrp;
	struct pgrp *pgrp; /* also protected by proc_list_lock */

	list_node_t node_proc;

	/* protected by proc_tree_lock */
	struct proc *parent; /* also protected by lock */
	list_t children;

	pid_t pid; /* (c) */
	struct vm_vas *vas; /* (c) */
	void *pls; /* process local storage, (c) */

	sync_t id_lock;
	uid_t euid;	/* effective uid */
	uid_t suid;	/* saved uid */
	uid_t uid;	/* real uid */
	gid_t gid;	/* real gid */
	gid_t egid;	/* effective gid */
	gid_t sgid;	/* saved gid */
} proc_t;

#define proc_local(proc, local) ((proc)->pls + (local)->off)
typedef struct proc_local {
	/* Filled by user */
	size_t size;
	int (*init) (struct proc *);
	int (*fork) (struct proc *dst, struct proc *src);
	void (*exec) (struct proc *);
	void (*exit) (struct proc *);
	/* private */
	size_t off;
} proc_local_t;

typedef enum thread_state {
	THREAD_SPAWNED = 0,
	THREAD_RUNNING,
	THREAD_RUNNABLE,
	THREAD_SLEEP,
	THREAD_EXIT,
} thread_state_t;

/*
 * Thread flags (remember that flags is just 8bit long).
 */
#define THREAD_IDLE		(1 << 0)

/*
 * Scheduler flags:
 * If the DO_SLEEP flag is set the thread will
 * not be scheduled anymore after the next preemption
 * (or call to schedule()) until it is woken up.
 */
#define THREAD_DO_SLEEP 	(1 << 0)
#define THREAD_INTERRUPTABLE	(1 << 1)
#define THREAD_INTERRUPTED 	(1 << 2)
#define THREAD_RESTARTSYS	(1 << 3)

/*
 * Thread interrupts
 */
#define THREAD_KILL 		(1 << 0) /* Call kern_exit asap */
#define THREAD_SIGNAL		(1 << 1) /* Call signal_intr asap */
#define THREAD_PROC		(1 << 2)

/**
 * @brief The thread structure.
 *
 * Remember to intialize new fields in thread_init
 */
typedef struct thread {
	arch_thread_t arch;

	/*
	 * Scheduler information, protected by scheduler.
	 */
	list_node_t sched_node;
	struct scheduler *sched;
	uint8_t prio;
	uint8_t sched_prio;
	uint8_t runq_idx;
	uint8_t sflags;
	uint8_t intr;

	/*
	 * The only flag that currently extist is IDLE. Thus this
	 * field is not protected.
	 */
	uint8_t flags;

	/*
	 * The state may not be changed and only accessed in _cur_thread_
	 * (but the scheduler code can change it nevertheless)
	 */
	thread_state_t state;

	/*
	 * Number if locked mutexes (not spinlocks).
	 */
	size_t numlock;
	uint8_t saved_prio;

	pid_t tid;

	union {
		struct {
			pid_t *set_child_tid;

			/*
			 * May only be changed by the thread itself.
			 */
			pid_t *clear_child_tid;
		};

		struct {
			int (*kfunc) (void *arg);
			void *karg;
		};
	};

	struct proc *proc; /* constant until exit */
	list_node_t proc_node;

	int exit_code;
	uint8_t exit_sig;

	void *kstack;
	void *tls; /* thread local storage */
	int syscall;
	struct jmp_buf *onfault;

	/* syscall trapframe */
	struct trapframe *trapframe;
} thread_t;

typedef struct thread_local {
	size_t size;

	int (*init) (struct thread *);
	void (*exit) (struct thread *);

	/* private stuff */
	size_t off;
} thread_local_t;

#define tls_call_err(thr, func, ...) \
	local_call_err(THREAD_LOCAL, thread_local_t, func, thr, ## __VA_ARGS__)
#define tls_call(thr, func, ...) \
	local_call(THREAD_LOCAL, thread_local_t, func, thr, ## __VA_ARGS__)
#define pls_call_err(thr, func, ...) \
	local_call_err(PROC_LOCAL, proc_local_t, func, thr, ## __VA_ARGS__)
#define pls_call(thr, func, ...) \
	local_call(PROC_LOCAL, proc_local_t, func, thr, ## __VA_ARGS__)

/* Thread & process local storage */
#ifndef __MODULE__
#define PROC_LOCAL	section(proclocal, proc_local_t)
#define define_pls(name) define_local_var(PROC_LOCAL, proc_local_t, name)
#define THREAD_LOCAL 	section(threadlocal, thread_local_t)
#define define_tls(name) define_local_var(THREAD_LOCAL, thread_local_t, name)
#endif

extern proc_t kernel_proc;
extern thread_t boot_thread;
extern sync_t proc_list_lock;

/* sched.c */
thread_t *cur_thread(void);
static inline proc_t *cur_proc(void) {
	return cur_thread()->proc;
}

/**
 * Check if a process was created using a call to fork().
 * If proc_was_forked returns false, the fork callback
 * of the process local storage won't be called and the
 * process local data has to be initialized in the init
 * callback.
 */
static inline bool proc_was_forked(proc_t *proc) {
	return proc->parent != NULL;
}

static inline bool proc_test_flags(proc_t *proc, int flags) {
	sync_assert(&proc->lock);
	return (proc->flags & flags) == flags;
}

static inline void proc_get_ids(uid_t *uid, gid_t *gid) {
	proc_t *proc = cur_proc();
	sync_scope_acquire(&proc->id_lock);
	*uid = proc->uid;
	*gid = proc->gid;
}

static inline void proc_get_eids(uid_t *uid, gid_t *gid) {
	proc_t *proc = cur_proc();
	sync_scope_acquire(&proc->id_lock);
	*uid = proc->euid;
	*gid = proc->egid;
}

static inline uid_t proc_euid(void) {
	proc_t *proc = cur_proc();
	sync_scope_acquire(&proc->id_lock);
	return proc->euid;
}

static inline void thread_set_flag(thread_t *thread, int flag) {
	kassert((flag & ~(THREAD_IDLE)) == 0, "[thread] unknown flag: %d",
		flag);
	atomic_or_relaxed(&thread->flags, flag);
}

static inline void thread_clear_flag(thread_t *thread, int flag) {
	kassert((flag & ~(THREAD_IDLE)) == 0, "[thread] unknown flag: %d",
		flag);
	atomic_and_relaxed(&thread->flags, ~flag);
}

static inline bool thread_test_flags(thread_t *thread, int flags) {
	kassert((flags & ~(THREAD_IDLE)) == 0, "[thread] unknown flag: %d",
		flags);
	return (atomic_load_relaxed(&thread->flags) & flags) == flags;
}

static inline bool thread_mayfault(void) {
	return cur_thread()->onfault != NULL;
}

static inline bool thread_is_kern(thread_t *thread) {
	return thread->tid == KTHREAD_TID;
}

static inline bool thread_is_user(thread_t *thread) {
	return thread->tid != KTHREAD_TID;
}

void init_proc(void);
void init_thread(void);

/* process.c */
/**
 * @brief	Stop a process. When stopping the current process, the calling
 *		thread is not being suspended.
 */
void proc_stop(proc_t *proc, int sig);

/**
 * @brief Continue a stopped process.
 */
void proc_cont(proc_t *proc);


/**
 * @brief Called at exec().
 */
void proc_exec(void);

/**
 * @brief Associate a process with a image name.
 */
void proc_set_image(proc_t *proc, proc_image_t *img);

/**
 * @brief	Called when a thread has exited (This is not called inside the
 *		context of the exiting thread).
 */
void proc_thread_exit(thread_t *thread);

/**
 * @brief Called if the last thread in the process no longer executes.
 */
void proc_exit_final(proc_t *proc);

/**
 * @brief Do not call directly (used in fork and kthread_spawn).
 */
void proc_add_thread(proc_t *proc, thread_t *thread);
void kproc_add_thread(thread_t *thr);

/**
 * @brief Spawn the first user proces
 */
void proc_spawn_init(void);

#define PROC_ST_KILL 0
#define PROC_ST_WAIT 1
#define PROC_ST_END  2
#define PROC_ST_DONE 3
#define PROC_ST_EXIT (1 << 2)
int proc_singlethread(int st);
void proc_wakeup_st_waiter(proc_t *proc);

/**
 * @brief Enable auto-reap mode in a process.
 *
 * When auto-reap mode is enabled, no SIGCHLD will be
 * sent to the process when a child exits. The kernel
 * manages reaping the child processes.
 */
void proc_autoreap_enable(proc_t *proc);

/**
 * @brief Disable auto-reap mode in a process.
 */
void proc_autoreap_disable(proc_t *proc);

/**
 * @brief Search for a process.
 *
 * Search for the process specified by a process
 * id. The proc_list_lock has to be acquired by
 * the current thread while calling.
 *
 * @return A pointer to the process or NULL
 */
proc_t *proc_lookup(pid_t id);

pid_t kern_getpgrp(void);

int proc_kill(pid_t id, int sig, int flags);

/**
 * @brief Get a structure in process local storage.
 */
void *pls_get(proc_local_t *l);
void *pls_get_proc(proc_t *p, proc_local_t *l);

/**
 * @brief Get/set the controlling tty of the current process's session.
 */
struct tty *session_get_tty(void);
int session_set_tty(struct tty *tty);

/**
 * @brief Check wheter a proccess group with pgid @p exists inside a session.
 */
bool pgrp_present(pid_t id, pid_t session);

/**
 * @brief Send a signal to every process inside of a process group.
 */
int pgrp_kill(pid_t id, int sig, int flags);

/* thread.c */

/**
 * Allocate a thread structure. The thread returned needs further setup
 * and may not be added to the scheduler.
 *
 * @param id 	An id allocated using tid_alloc for user threads or KTHREAD_TID
 *		for kernel threads
 */
int thread_new(pid_t id, thread_t **thrp);

/**
 * @brief Free a thread structure returned by thread_new.
 */
void thread_free(thread_t *thread);

/**
 * @brief Get a structure in thread local storage.
 */
void *tls_get(thread_local_t *l);
void *tls_thread_get(thread_t *t, thread_local_t *l);

/**
 * @brief 	Temporarily increase the priority of a thread
 * @return 	The old priority of the thread used for
 *		thread_prio_pop
 */
enum sched_prio thread_prio_push(enum sched_prio prio);

/**
 * @brief Reverse a thred_prio_push
 * @param tmp The value returned by thread_prio_push
 */
void thread_prio_pop(enum sched_prio tmp);

/**
 * @brief Allocate/Free a thread id
 */
pid_t tid_alloc(void);
void tid_free(pid_t tid);

/**
 * @brief Increase/Decrease the number of mutexes a thread holds.
 */
void thread_numlock_inc(void);
void thread_numlock_dec(void);

/**
 * Returns true if the thread was woken up by another thread, because
 * this thread has to e.g. handle a signal or exit.
 */
bool thread_interrupted(void);

/**
 * Called when a thread is about to return from an interrupt to userspace.
 */
void thread_uret(void);

/**
 * @brief Force another thread to exit
 */
void thread_kill(thread_t *thread);

/**
 * @brief Tell the thread that it should call signal_intr().
 */
void thread_signal(thread_t *thread, bool restart);

/**
 * Wakeup a thread and make sure that it should check for the status of the
 * process (single-thread-mode, or STOP signal).
 */
void thread_intr_proc(thread_t *thread);

/**
 * Allocate a new kernel thread, but do not add the thread to the
 * scheduler (Used for allocating the cpu's idle thread)
 */
thread_t *kthread_alloc(int (*func) (void *), void *arg);

/**
 * @brief Create a new kernel thread and add it add it to the scheduler
 */
thread_t *kthread_spawn_prio(int (*func) (void *), void *arg, uint8_t prio);
thread_t *kthread_spawn(int (*func) (void *), void *arg);

void thread_clear_tid(void);
void __noreturn thread_do_exit(void);
void __noreturn kern_exit(int ret);
void __noreturn kern_exitproc(int status, int sig);

void thread_fork_ret(void);

/*
 * syscalls
 */
pid_t sys_getpid(void);
pid_t sys_getppid(void);
pid_t sys_setsid(void);
pid_t sys_getsid(pid_t pid);
pid_t sys_getpgrp(void);
pid_t sys_getpgid(pid_t pid);
int sys_setpgid(pid_t pid, pid_t pgid);
pid_t sys_fork(void);
pid_t sys_vfork(void);
int __noreturn sys_exit_group(int status);
int __noreturn sys_exit(int code);
pid_t sys_wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage);
int sys_getuid32(void);
int sys_geteuid32(void);
int sys_getgid32(void);
int sys_getegid32(void);
int sys_setuid32(uid_t id);
int sys_setgid32(gid_t id);
int sys_setresgid32(gid_t rgid, gid_t egid, gid_t sgid);
int sys_setgroups32(int size, gid_t *list);
int sys_getgroups32(int size, gid_t *list);
int sys_set_tid_address(int *addr);
int sys_gettid(void);
int sys_clone(int flags, int stack, pid_t *ptid, struct user_desc *desc,
	pid_t *ctid);
int sys_getrusage(int who, struct rusage *usage);
int sys_prlimit64(pid_t pid, int resource, const struct rlimit *new_limit,
	struct rlimit *old_limit);

#endif

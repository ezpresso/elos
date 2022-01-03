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
#include <kern/sched.h>
#include <kern/exec.h>
#include <kern/tty.h>
#include <kern/signal.h>
#include <kern/user.h>
#include <kern/futex.h>
#include <vm/vas.h>
#include <vm/malloc.h>
#include <vm/slab.h>
#include <lib/bitset.h>
#include <lib/hashtab.h>
#include <lib/list-locked.h>
#include <sys/limits.h>
#include <sys/wait.h>

static proc_image_t kernel_img = {
	.ref = 1,
	.binary = "kernel",
};

proc_t kernel_proc = {
	.image = &kernel_img
};
sync_t proc_list_lock = SYNC_INIT(MUTEX);

static DEFINE_VM_SLAB(pgrp_cache, sizeof(pgrp_t), 0);
static DEFINE_VM_SLAB(sess_cache, sizeof(session_t), 0);
static size_t proc_local_size;
static proc_t *init_process;
static sync_t proc_tree_lock = SYNC_INIT(MUTEX);
static hashtab_t proc_list, pgrp_list;

void *pls_get(proc_local_t *l) {
	proc_t *proc = cur_proc();
	assert(proc);
	assert(!proc_test_flags(proc, PROC_EXIT));
	return pls_get_proc(proc, l);
}

void *pls_get_proc(proc_t *p, proc_local_t *l) {
	assert(p != &kernel_proc);
	return p->pls + l->off;
}

/**
 * @brief Initialize a process structure.
 */
static void proc_init(proc_t *proc, vm_vas_t *vas, proc_t *parent, pid_t pid,
	void *pls)
{
	list_init(&proc->children);
	list_init(&proc->threads);
	list_node_init(proc, &proc->node_child);
	list_node_init(proc, &proc->node_proc);
	list_node_init(proc, &proc->node_pgrp);
	sync_init(&proc->lock, SYNC_MUTEX);
	sync_init(&proc->id_lock, SYNC_SPINLOCK);
	waitqueue_init(&proc->waitq);

	proc->parent = parent;
	proc->vas = vas;
	proc->pid = pid;
	proc->pls = pls;
}

/**
 * @brief Free a process structure.
 */
static void proc_free(proc_t *proc) {
	proc_set_image(proc, NULL);
	list_destroy(&proc->children);
	list_destroy(&proc->threads);
	list_node_destroy(&proc->node_child);
	list_node_destroy(&proc->node_proc);
	list_node_destroy(&proc->node_pgrp);
	sync_destroy(&proc->lock);
	sync_destroy(&proc->id_lock);
	waitqueue_destroy(&proc->waitq);
	kfree(proc);
}

/**
 * @brief Allocate a new process.
 */
static int proc_new(proc_t *parent, pid_t id, proc_t **procp) {
	vm_vas_t *vas;
	proc_t *proc;
	void *pls;
	int err;

	assert(proc_local_size);

	/*
	 * Allocate a new process structure.
	 */
	proc = kmalloc(sizeof(*proc) + proc_local_size, VM_ZERO);
	if(!proc) {
		return -ENOMEM;
	}

	vas = vm_user_vas_alloc();
	pls = (void *)&proc[1];
	proc_init(proc, vas, parent, id, pls);

	err = pls_call_err(proc, init);
	if(err) {
		kfree(proc);
		return -ENOMEM;
	}

	*procp = proc;
	return 0;
}

/**
 * @brief Set some flags of a process.
 *
 * Set some flags of a process. Some sanity checks are performed
 * before setting specific flags. The caller must hold
 * the lock of the process.
 */
static void proc_set_flag(proc_t *proc, int flag) {
	sync_assert(&proc->lock);

	if(flag & PROC_FREE) {
		assert(proc_test_flags(proc, PROC_EXIT));
		assert(proc->st_mode == PROC_ST_KILL);
	}
	if(flag & PROC_ZOMBIE) {
		assert(list_length(&proc->threads) == 0);
		assert(!proc_test_flags(proc, PROC_ZOMBIE));
		assert(proc_test_flags(proc, PROC_FREE | PROC_EXIT));
	}
	if(flag & PROC_ST) {
		assert(!F_ISSET(proc->flags, PROC_ST));
	}
	if(flag & PROC_STOP) {
		assert(F_ISSET(flag, PROC_STATUS));
	}

	proc->flags |= flag;
}

/**
 * @brief Clear some flags of a process.
 *
 * Clear some flags of a process. The caller must hold
 * the lock of the process.
 */
static inline void proc_clear_flag(proc_t *proc, int flag) {
	sync_assert(&proc->lock);
	proc->flags &= ~flag;
}

void proc_add_thread(proc_t *proc, thread_t *thread) {
	sync_assert(&proc->lock);
	list_append(&proc->threads, &thread->proc_node);
	thread->proc = proc;
}

void proc_rmv_thread(proc_t *proc, thread_t *thread) {
	sync_assert(&proc->lock);
	list_remove(&proc->threads, &thread->proc_node);

	/*
	 * proc_singlethread might be waiting for threads to exit.
	 */
	proc_wakeup_st_waiter(proc);
}

proc_t *proc_lookup(pid_t id) {
	proc_t *proc;

	sync_assert(&proc_list_lock);
	hashtab_search(proc, id, &proc_list) {
		if(proc->pid == id) {
			return proc;
		}
	}

	return NULL;
}

int proc_kill(pid_t id, int sig, int flags) {
	proc_t *proc, *cur = cur_proc();

	assert(id > 0);
	sync_scope_acquire(&proc_list_lock);
	if(id == cur->pid) {
		proc = cur;
	} else {
		proc = proc_lookup(id);
		if(!proc) {
			return -ESRCH;
		}
	}

	/* TODO can unlock proc_list_lock */
	sync_scope_acquire(&proc->lock);
	return kern_kill(proc, sig, flags);
}

int proc_singlethread(int st) {
	thread_t *thread = cur_thread(), *cur;
	proc_t *proc = thread->proc;
	int flags, exit;
	size_t num;

	exit = st & PROC_ST_EXIT;
	st = st & ~PROC_ST_EXIT;

	sync_scope_acquire(&proc->lock);
	if(st == PROC_ST_END) {
		assert(proc_test_flags(proc, PROC_ST));
		assert(proc->st_thread == thread);

		proc->flags &= ~PROC_ST;
		proc->st_thread = NULL;
		kern_wake(&proc->flags, INT_MAX, 0);

		return 0;
	}

	flags = proc->flags;

	/*
	 * If the process is going to exit and the current thread
	 * is not the one exiting the process, abort the single
	 * thread request.
	 */
	if(!exit && proc_test_flags(proc, PROC_EXIT)) {
		return -EINTR;
	}

	/*
	 * If another thread is currently in single-thread mode, abort
	 * this request and get as fast to thread_uret() as possible.
	 * However the current thread might have told the other threads
	 * to wait in thread_uret() and those threads waiting now need
	 * to be killed (see execve()).
	 */
	if(proc_test_flags(proc, PROC_ST)) {
		if(proc->st_thread == thread) {
			assert(proc->st_mode == PROC_ST_WAIT);
			assert(st == PROC_ST_KILL);
		} else {
			/*
			 * Another thread might also want single threading mode.
			 */
			assert(thread_interrupted());
			return -EINTR;
		}
	} else {
		/*
		 * Stop other threads from requesting single-thread mode.
		 */
		proc_set_flag(proc, PROC_ST);
	}

	proc->st_thread = thread;
	proc->st_mode = st;
	assert(proc->st_waiting == 0);

	/*
	 * Interrupt all the threads, except the current thread of course.
	 * The others will either be killed or wait in thread_uret().
	 */
	foreach(cur, &proc->threads) {
		if(cur != thread) {
			if(st == PROC_ST_WAIT) {
				thread_intr_proc(cur);
			} else {
				thread_kill(cur);
			}
		}
	}

	/*
	 * If a thread tells the process to enter WAIT mode (thus all of
	 * thre proc's threads are waiting in thread_uret()) it will
	 * afterwards either END the single-thread mode or enter
	 * KILL mode to kill the waiting threads. Before the waiting
	 * threads can be killed, they have to be woken up again.
	 * The threads have to be woken up after calling thread_kill(),
	 * on them, because if it was the other way around there would
	 * be a chance that they go back to usermode, which is not
	 * allowed here.
	 */
	if(proc->st_mode == PROC_ST_KILL && (flags & PROC_ST)) {
		kern_wake(&proc->flags, INT_MAX, 0);
	}

	/*
	 * Wait for the other threads.
	 */
	if(st == PROC_ST_KILL) {
		while((num = list_length(&proc->threads)) > 1) {
			sync_release(&proc->lock);
			kern_wait(list_length_ptr(&proc->threads), num, 0);
			sync_acquire(&proc->lock);
			if(!exit && proc_test_flags(proc, PROC_EXIT)) {
				return -EINTR;
			}
		}

		/*
		 * Success. Since there is no thread anymore, the
		 * single-thread-flag can now be cleared.
		 */
		proc->flags &= ~PROC_ST;
		proc->st_thread = NULL;
	} else while((num = proc->st_waiting) !=
		list_length(&proc->threads) - 1)
	{
		sync_release(&proc->lock);
		kern_wait(&proc->st_waiting, num, 0);
		sync_acquire(&proc->lock);

		if(!exit && proc_test_flags(proc, PROC_EXIT)) {
			return -EINTR;
		}
	}

	return 0;
}

void proc_wakeup_st_waiter(proc_t *proc) {
	sync_assert(&proc->lock);

	if(proc_test_flags(proc, PROC_ST)) {
		assert(proc->st_thread != cur_thread());

		/*
		 * TODO make sure that is called on every exit
		 */
		if(proc->st_mode == PROC_ST_KILL) {
			kern_wake(list_length_ptr(&proc->threads), 1, 0);
		} else {
			assert(proc->st_mode == PROC_ST_WAIT);
			kern_wake(&proc->st_waiting, 1, 0);
		}
	}
}

void proc_autoreap_enable(proc_t *proc) {
	sync_scope_acquire(&proc->lock);
	proc_set_flag(proc, PROC_AUTOREAP);
}

void proc_autoreap_disable(proc_t *proc) {
	sync_scope_acquire(&proc->lock);
	proc_clear_flag(proc, PROC_AUTOREAP);
}

/**
 * @brief Send a SIGCHLD signal to the parent of a process.
 *
 * The caller must hold the lock of the parent process.
 */
static inline void proc_do_sigchld(proc_t *proc) {
	sync_assert(&proc->parent->lock);
	kern_kill(proc->parent, SIGCHLD, SIG_KERN);
	wakeup(&proc->parent->waitq, SCHED_NORMAL);
}

/**
 * @brief Send a SIGCHLD signal to the parent of a process.
 *
 * The caller must hold the lock of the process. Since the
 * parent process needs to be locked during this operation
 * the lock of the process is released and afterwards acquired
 * again
 */
static inline void proc_sigchld(proc_t *proc) {
	sync_assert(&proc->lock);
	sync_release(&proc->lock);

	synchronized(&proc_tree_lock) {
		sync_scope_acquire(&proc->parent->lock);
		proc_do_sigchld(proc);
	}

	sync_acquire(&proc->lock);
}

void proc_stop(proc_t *proc, int sig) {
	sync_assert(&proc->lock);

	if(proc_test_flags(proc, PROC_STOP) == false) {
		thread_t *thr;

		proc_set_flag(proc, PROC_STOP | PROC_STATUS);
		proc->stop_sig = sig;

		foreach(thr, &proc->threads) {
			thread_intr_proc(thr);
		}

		proc_sigchld(proc);
	}
}

void proc_cont(proc_t *proc) {
	sync_assert(&proc->lock);

	if(proc_test_flags(proc, PROC_STOP) == true) {
		proc->flags = (proc->flags & ~PROC_STOP) | PROC_STATUS;

		/*
		 * Resume the threads of the process.
		 */
		kern_wake(&proc->flags, INT_MAX, 0);

		/*
		 * Inform parent of the status change.
		 */
		proc_sigchld(proc);
	}
}

void proc_exec(void) {
	proc_t *proc = cur_proc();

	pls_call(proc, exec);

	synchronized(&proc->lock) {
		proc->flags |= PROC_EXEC;

		/*
		 * Wakeup parent if it's waiting for vfork.
		 */
		if(proc->parent) {
			wakeup(&proc->parent->waitq, SCHED_NORMAL);
		}
	}
}

void proc_set_image(proc_t *proc, proc_image_t *img) {
	if(proc->image && ref_dec(&proc->image->ref)) {
		kfree(proc->image);
	}

	proc->image = img;
}

/**
 * @brief Allocate a new session.
 */
static session_t *session_alloc(pid_t id) {
	session_t *sess;

	sess = vm_slab_alloc(&sess_cache, VM_WAIT);
	sync_init(&sess->lock, SYNC_MUTEX);
	ref_init(&sess->ref);
	sess->id = id;
	sess->tty = NULL;

	return sess;
}

/**
 * @brief Free a sesson.
 */
static void session_free(session_t *sess) {
	sync_destroy(&sess->lock);
	vm_slab_free(&sess_cache, sess);
}

/**
 * @brief Increment the reference count of a session.
 */
static session_t *session_ref(session_t *sess) {
	ref_inc(&sess->ref);
	return sess;
}

/**
 * @brief Decrease the reference count of a session.
 */
static void session_unref(session_t *sess) {
	if(ref_dec(&sess->ref)) {
		/*
		 * If a session is freed, the session leader has
		 * definitely exited. Furhtermore it means that
		 * there is no pgrp left that session. Since
		 * process groups cannot change their session,
		 * the process group with that pid is also dead.
		 * As a result, it is safe to free the tid.
		 */
		tid_free(sess->id);
		session_free(sess);
	}
}

tty_t *session_get_tty(void) {
	proc_t *proc = cur_proc();
	session_t *sess;

	assert(proc);
	sync_scope_acquire(&proc_list_lock);
	sess = proc->pgrp->session;
	sync_scope_acquire(&sess->lock);

	if(sess->tty == NULL) {
		return NULL;
	} else {
		return tty_ref(sess->tty);
	}
}

int session_set_tty(tty_t *tty) {
	proc_t *proc = cur_proc();
	session_t *sess;

	synchronized(&proc_list_lock) {
		sess = session_ref(proc->pgrp->session);
	}

	/*
	 * Only the session leader may change the
	 * controlling terminal of the session
	 */
	if(sess->id != proc->pid) {
		goto err;
	}

	synchronized(&tty->lock) {
		sync_scope_acquire(&sess->lock);

		/*
		 * It's not allowed to change the ctty if there is already a
		 * controlling tty
		 */
		if(tty->session != 0 ||
			(sess->tty != NULL && sess->tty != tty))
		{
			goto err;
		}

		if(sess->tty == NULL) {
			sess->tty = tty_ref(tty);
			tty->session = sess->id;
		}
	}

	session_unref(sess);

	return 0;

err:
	session_unref(sess);
	return -EPERM;
}

/**
 * @brief Allocate a new process group.
 */
static pgrp_t *pgrp_alloc(void) {
	pgrp_t *grp;

	grp = vm_slab_alloc(&pgrp_cache, VM_WAIT);
	sync_init(&grp->lock, SYNC_MUTEX);
	list_init(&grp->members);
	list_node_init(grp, &grp->node);
	grp->session = NULL;
	grp->id = 0;

	return grp;
}

/**
 * @brief Free a process group.
 */
static void pgrp_free(pgrp_t *grp) {
	if(grp->session) {
		session_unref(grp->session);
	}

	sync_destroy(&grp->lock);
	list_destroy(&grp->members);
	list_node_destroy(&grp->node);
	vm_slab_free(&pgrp_cache, grp);
}

/**
 * @brief Associate a process group with a process and a session.
 */
static void pgrp_setup(pgrp_t *grp, pid_t pid, session_t *sess) {
	sync_assert(&proc_list_lock);

	grp->session = session_ref(sess);
	grp->id = pid;
	hashtab_set(&pgrp_list, grp->id, &grp->node);
}

/**
 * @brief Add a process to a process group.
 *
 * The caller must hold the proc_list_lock.
 */
static int pgrp_enter(proc_t *proc, pgrp_t *grp) {
	bool empty = false;
	pgrp_t *old;
	int err = 0;

	sync_assert(&proc_list_lock);

	/*
	 * This is safe because of proc_list_lock.
	 */
	old = proc->pgrp;
	assert(old != grp);

	/*
	 * Have to be really careful with the locks here
	 * in order to prevent deadlocks.
	 */
	if(grp) {
		sync_acquire(&grp->lock);
	}
	if(old) {
		sync_acquire(&old->lock);
	}

	sync_acquire(&proc->lock);

	/*
	 * If changing the process group id of a child process, the child
	 * process may not have called execve().
	 */
	if(proc != cur_proc() && proc_test_flags(proc, PROC_EXEC)
		&& !proc_test_flags(proc, PROC_ZOMBIE))
	{
		err = -EACCES;
	} else {
		proc->pgrp = grp;
		if(old) {
			empty = list_remove(&old->members, &proc->node_pgrp);
		}

		if(grp) {
			list_append(&grp->members, &proc->node_pgrp);
		}
	}

	sync_release(&proc->lock);
	if(old) {
		sync_release(&old->lock);
	}
	if(grp) {
		sync_release(&grp->lock);
	}

	if(empty) {
		/*
		 * If there is no process/session with that id, free it.
		 */
		if(proc_lookup(old->id) == NULL &&
			old->session->id != old->id)
		{
			tid_free(old->id);
		}

		hashtab_remove(&pgrp_list, old->id, &old->node);
		pgrp_free(old);
	}

	return err;
}

/**
 * @brief Lookup a process group.
 *
 * The caller must hold the proc_list_lock.
 */
static pgrp_t *pgrp_lookup(pid_t id) {
	pgrp_t *grp;

	sync_assert(&proc_list_lock);

	hashtab_search(grp, id, &pgrp_list) {
		if(grp->id == id) {
			return grp;
		}
	}

	return NULL;
}

bool pgrp_present(pid_t id, pid_t session) {
	pgrp_t *grp;

	sync_assert(&proc_list_lock);
	grp = pgrp_lookup(id);
	if(!grp || grp->session->id != session) {
		return false;
	}

	return true;
}

int pgrp_kill(pid_t id, int sig, int flags) {
	int last_err = 0, num = 0;
	proc_t *proc;
	pgrp_t *pgrp;

	assert(id == 0 || id > 1);
	synchronized(&proc_list_lock) {
		if(id == 0) {
			pgrp = cur_proc()->pgrp;
		} else {
			pgrp = pgrp_lookup(id);
			if(!pgrp) {
				return -ESRCH;
			}
		}

		sync_acquire(&pgrp->lock);
	}

	foreach(proc, &pgrp->members) {
		int err = 0;

		synchronized(&proc->lock) {
			err = kern_kill(proc, sig, flags);
		}
		if(err) {
			last_err = err;
		} else {
			num++;
		}
	}

	sync_release(&pgrp->lock);

	if(num == 0) {
		return last_err;
	}

	/*
	 * If at least one signal was sent return success!
	 */
	return 0;
}

pid_t sys_fork(void) {
	proc_t *proc = cur_proc();
	thread_t *thread;
	proc_t *new;
	pid_t pid;
	int err;

	assert(proc != &kernel_proc);
	pid = tid_alloc();
	if(pid < 0) {
		return pid;
	}

	assert(pid > 1);

	/*
	 * Allocate a process and thread structure.
	 */
	err = proc_new(proc, pid, &new);
	if(err) {
		goto err_new;
	}

	err = thread_new(pid, &thread);
	if(err) {
		goto err_thr;
	}

	/*
	 * Add the thread to the process, because pls_call_err may
	 * want to so something with the thread (e.g. sigproc_fork).
	 * The lock is just to make sure no sync_assert fail.
	 */
	synchronized(&new->lock) {
		proc_add_thread(new, thread);
	}

	/*
	 * Fork all the rest (signals, filedescs, etc.).
	 */
	err = pls_call_err(new, fork, proc);
	if(err) {
		goto err_pls;
	}

	/*
	 * Create a copy of the complete virtual address space.
	 */
	vm_vas_fork(new->vas, proc->vas);

	/*
	 * Copy the user and group ids.
	 */
	synchronized(&proc->id_lock) {
		new->uid = proc->uid;
		new->euid = proc->euid;
		new->suid = proc->suid;
		new->gid = proc->gid;
		new->egid = proc->egid;
		new->sgid = proc->sgid;
	}

	ref_inc(&proc->image->ref);
	new->image = proc->image;

	arch_thread_fork(thread, cur_thread(), 0, 0);
	synchronized(&proc_tree_lock) {
		list_append(&proc->children, &new->node_child);

		synchronized(&proc_list_lock) {
			/*
			 * Enter the process group of the parent.
			 */
			err = pgrp_enter(new, proc->pgrp);
			assert(!err);

			hashtab_set(&proc_list, new->pid, &new->node_proc);

			/*
			 * The new thread is now ready to run.
			 */
			sched_add_thread(thread);
		}
	}

	return pid;

err_pls:
	synchronized(&new->lock) {
		proc_rmv_thread(new, thread);
	}
	thread_free(thread);
err_thr:
	proc_free(new);
err_new:
	tid_free(pid);
	return err;
}

/**
 * @brief Check whether a process can be reaped.
 *
 * The caller must hold the proc_tree_lock.
 *
 * @return 	-1 no such process
 *			 1 process present but not a zombie
 *			 0 zombie process present
 */
static bool proc_can_reap(pid_t pid, proc_t *parent, proc_t *child) {
	sync_assert(&proc_tree_lock);

	sync_scope_acquire(&parent->lock);
	sync_scope_acquire(&child->lock);

	if(pid < -1) {
		if(child->pgrp->id != -pid) {
			return -1;
		}
	} else if(pid == 0) {
		if(child->pgrp->id != parent->pgrp->id) {
			return -1;
		}
	} else if(pid > 0) {
		if(child->pid != pid) {
			return -1;
		}
	}

	if(proc_test_flags(child, PROC_ZOMBIE) == false) {
		return 1;
	} else {
		return 0;
	}
}

pid_t sys_vfork(void) {
	proc_t *proc = cur_proc();
	waiter_t wait;
	pid_t pid;
	int err;

	/*
	 * Enter single-thread-mode.
	 */
	err = proc_singlethread(PROC_ST_WAIT);
	if(err) {
		return -ERESTART;
	}

	pid = sys_fork();
	if(pid < 0) {
		goto out;
	}

	waiter_init(&wait);

	/*
	 * Wait for child to complete or call execve.
	 */
	while(true) {
		proc_t *child = NULL;

		wait_prep(&proc->waitq, &wait);

		synchronized(&proc_tree_lock) {
			synchronized(&proc_list_lock) {
				child = proc_lookup(pid);
			}

			if(child) {
				if(proc_can_reap(pid, proc, child) == 0) {
					child = NULL;
				} else {
					sync_scope_acquire(&child->lock);
					if(proc_test_flags(child, PROC_EXEC)) {
						child = NULL;
					}
				}
			}
		}

		if(child == NULL) {
			wait_abort(&proc->waitq, &wait);
			waiter_destroy(&wait);
			goto out;
		} else {
			wait_sleep(&proc->waitq, &wait, 0);
		}
	}

	notreached();

out:
	proc_singlethread(PROC_ST_END);
	return pid;
}

/**
 * @brief Free a child zombie process.
 *
 * The caller must hold the proc_tree_lock.
 */
static void proc_reap(proc_t *parent, proc_t *child, int *status) {
	sync_assert(&proc_tree_lock);
	assert(parent == child->parent);

	/*
	 * Not a child anymore.
	 */
	list_remove(&parent->children, &child->node_child);

	synchronized(&proc_list_lock) {
		/*
		 * There may still be a process group or a session with
		 * that id. This session has to be the session of the
		 * process since a session leader cannot change its
		 * session.
		 */
		if(pgrp_lookup(child->pid) == NULL &&
			child->pgrp->session->id != child->pid)
		{
			tid_free(child->pid);
		}

		/*
		 * Leave the process group.
		 */
		pgrp_enter(child, NULL);
		if(status) {
			*status = W_EXITCODE(child->exit_code,
				child->exit_sig, 0);
		}
	}

	/*
	 * It's safe to free the process now.
	 */
	proc_free(child);
}

pid_t sys_wait4(pid_t pid, int *wstatus, int options,
	__unused struct rusage *rusage)
{
	proc_t *proc = cur_proc(), *child;
	int res, err, status = 0;
	waiter_t wait;
	pid_t retval;
	size_t nfound;

	waiter_init(&wait);
	while(true) {
		nfound = 0;

		wait_prep(&proc->waitq, &wait);

		/*
		 * Scan through the child processes.
		 */
		foreach_locked(child, &proc->children, &proc_tree_lock) {
			retval = child->pid;

			res = proc_can_reap(pid, proc, child);
			if(res == 0) {
				wait_abort(&proc->waitq, &wait);
				proc_reap(proc, child, &status);
				goto success;
			} else if(res == -1) {
				continue;
			}

			nfound++;
			if(!(options & (WUNTRACED | WCONTINUED))) {
				continue;
			}

			/*
			 * The process is not a zombie, however the status
			 * of the process might have hanged.
			 */
			sync_scope_acquire(&child->lock);
			if(proc_test_flags(child, PROC_STATUS)) {
				proc_clear_flag(child, PROC_STATUS);
				status = 0;

				if(proc_test_flags(child, PROC_STOP)) {
					if(options & WUNTRACED) {
						status = W_STOPCODE(
							child->stop_sig);
					}
				} else {
					if(options & WCONTINUED) {
						status = W_CONTCODE;
					}
				}

				if(status) {
					wait_abort(&proc->waitq, &wait);
					goto success;
				}
			}
		}

		/*
		 * pid does not point to any child process of this process.
		 */
		if(nfound == 0) {
			wait_abort(&proc->waitq, &wait);
			retval = -ECHILD;
			goto out;
		}

		/*
		 * Don't wait, if the caller does not want to.
		 */
		if(options & WNOHANG) {
			wait_abort(&proc->waitq, &wait);
			retval = 0;
			goto out;
		}

		/*
		 * Wait for children to change state.
		 */
		err = wait_sleep(&proc->waitq, &wait, WAIT_INTERRUPTABLE);
		if(err) {
			retval = err;
			goto out;
		}
	}

	notreached();

success:
	if(wstatus) {
		err = copyout(wstatus, &status, sizeof(int));
		if(err) {
			retval = err;
		}
	}
out:
	waiter_destroy(&wait);
	return retval;
}

/**
 * @brief Change the parent of a process.
 *
 * The caller must hold the proc_tree_lock.
 */
static void proc_reparent(proc_t *proc, proc_t *parent) {
	proc_t *old;

	sync_assert(&proc_tree_lock);
	synchronized(&proc->lock) {
		old = proc->parent;
		list_remove(&old->children, &proc->node_child);
		list_append_locked(&parent->children, &proc->node_child,
				&parent->lock);
		proc->parent = parent;
	}
}

/**
 * @brief Exit a process.
 */
static void proc_exit(int status, int sig) {
	proc_t *proc = cur_proc(), *child;
	thread_t *cur_thr = cur_thread();

	if(proc == init_process) {
		kpanic("[proc] init exited with status: %d", status);
	}

	synchronized(&proc->lock) {
		/*
		 * Handle multiple concurrent calls to proc_exit, to make sure
		 * that only one thread can exit the process.
		 */
		if(proc_test_flags(proc, PROC_EXIT)) {
			assert(thread_interrupted());
			return;
		} else {
			proc_set_flag(proc, PROC_EXIT);
		}
	}

	if(proc_singlethread(PROC_ST_KILL | PROC_ST_EXIT) != 0) {
		int err, st = 0;

		assert(thread_interrupted());

		/*
		 * If there is already a thread wanting singlethread mode,
		 * proc_singlethread may fail. The thread requesting the single
		 * thread mode has to be waiting, because this thread is still
		 * running. Abort the waiting of the other thread.
		 */
		synchronized(&proc->lock) {
			proc_wakeup_st_waiter(proc);
			proc_clear_flag(proc, PROC_ST);
			proc->st_thread = NULL;
			st = proc->st_mode;
		}

		if(st == PROC_ST_WAIT) {
			/*
			 * Some threads might still be waiting. We have to wake
			 * them up manually in this case.
			 */
			kern_wake(&proc->flags, INT_MAX, 0);
		}

		err = proc_singlethread(PROC_ST_KILL);
		assert(err == 0);
	}

	/*
	 * The init process is the new parent of the process's children.
	 */
	foreach_locked(child, &proc->children, &proc_tree_lock) {
		proc_reparent(child, init_process);
	}

	/*
	 * Remove the process from the global list.
	 */
	synchronized(&proc_list_lock) {
		hashtab_remove(&proc_list, proc->pid, &proc->node_proc);
	}

	synchronized(&proc->lock) {
		proc->exit_code = status;
		proc->exit_sig = sig;

		/*
		 * Remove the current thread from the process's thread list.
		 */
		list_remove(&proc->threads, &cur_thr->proc_node);

		/*
		 * This flag can only be set after proc_singlethread.
		 */
		proc_set_flag(proc, PROC_FREE);
	}

	/*
	 * Free the user parts of the virtual address of the process. The
	 * kernel space part is still left intact.
	 */
	pls_call(proc, exit);
	vm_vas_unmap(proc->vas, vm_vas_start(proc->vas),
		vm_vas_size(proc->vas));
}

void proc_exit_final(proc_t *proc) {
	assert(proc != cur_proc());
	assert(cur_thread()->sched);

	/*
	 * Now that no thread of this process is running anymore,
	 * the vas structure can be freed (the userspace parts
	 * of the vas are already freed).
	 */
	vm_user_vas_free(proc->vas);

	sync_scope_acquire(&proc_tree_lock);
	sync_acquire(&proc->parent->lock);

	if(proc_test_flags(proc->parent, PROC_AUTOREAP)) {
		sync_release(&proc->parent->lock);
		proc_reap(proc->parent, proc, NULL);
	} else {
		sync_scope_acquire(&proc->lock);

		/*
		 * The process structure can now be freed by the parent.
		 */
		proc_set_flag(proc, PROC_ZOMBIE);

		/*
		 * Wakeup the parent if it's sleeping due to wait4() and
		 * send a SIGCHLD signal to the parent.
		 */
		proc_do_sigchld(proc);
		sync_release(&proc->parent->lock);
	}
}

void __noreturn kern_exit(__unused int ret) {
	thread_t *thread = cur_thread();
	proc_t *proc = cur_proc();

	/*
	 * Have to do this before proc_exit, because proc_exit will free
	 * destroy the user-parts of vmctx.
	 */
	thread_clear_tid();

	if(thread->tid != -1 && thread->tid != proc->pid) {
		/*
		 * The thread's id is not equal to the process's id,
		 * which means there may not be any process group
		 * or session or even a process with that id!
		 */
		tid_free(thread->tid);

		/*
		 * Set thread->tid to a dummy value.
		 */
		thread->tid = -111;
	}

	sync_acquire(&proc->lock);
	if(list_length(&proc->threads) == 1) {
		sync_release(&proc->lock);

		/*
		 * If there are no threads left in the proccess,
		 * exit the process.
		 */
		proc_exit(0, 0);
	} else {
		proc_rmv_thread(proc, thread);
		sync_release(&proc->lock);
	}

	thread_do_exit();
}

void __noreturn kern_exitproc(int status, int sig) {
	thread_clear_tid();
	proc_exit(status, sig);
	thread_do_exit();
}

int __noreturn sys_exit_group(int status) {
	kern_exitproc(status, 0);
}

pid_t kern_getpgrp(void) {
	proc_t *proc = cur_proc();
	sync_scope_acquire(&proc->lock);
	return proc->pgrp->id;
}

pid_t sys_getpid(void) {
	return cur_proc()->pid;
}

pid_t sys_getppid(void) {
	proc_t *proc = cur_proc();

	sync_scope_acquire(&proc->lock);

	/*
	 * The init process may call getppid too.
	 */
	if(proc->parent == NULL) {
		/*
		 * Don't know what to return here.
		 */
		return proc->pid;
	}

	return proc->parent->pid;
}

pid_t sys_setsid(void) {
	proc_t *proc = cur_proc();
	session_t *sess;
	pgrp_t *pgrp, *tmp;
	int err;

	sess = session_alloc(proc->pid);
	pgrp = pgrp_alloc();

	synchronized(&proc_list_lock) {
		if(proc->pgrp->id == proc->pid ||
			(tmp = pgrp_lookup(proc->pid)))
		{
			err = -EPERM;
			goto err_perm;
		}

		pgrp_setup(pgrp, proc->pid, sess);
		err = pgrp_enter(proc, pgrp);
		assert(!err);
	}

	return proc->pid;

err_perm:
	pgrp_free(pgrp);

	/*
	 * Remember to not call session_unref here, because the unref may
	 * want to free proc->pid.
	 */
	session_free(sess);
	return err;
}

pid_t sys_getsid(pid_t pid) {
	proc_t *cur = cur_proc(), *tmp;

	if(pid != 0 && pid != cur->pid) {
		synchronized(&proc_list_lock) {
			tmp = proc_lookup(pid);
			if(!tmp) {
				return -ESRCH;
			} else if(tmp->pgrp->session != cur->pgrp->session) {
				return -EPERM;
			}
		}
	}

	/*
	 * If the process described by pid is not in
	 * the same session as the current process
	 * -EPERM is returned -> the sid returned
	 * is always equal to the sid of the current
	 * process
	 */
	sync_scope_acquire(&cur->lock);
	return cur->pgrp->session->id;
}

pid_t sys_getpgrp(void) {
	return kern_getpgrp();
}

pid_t sys_getpgid(pid_t pid) {
	proc_t *proc, *cur = cur_proc();

	sync_scope_acquire(&proc_list_lock);
	if(pid == 0 || pid == cur->pid) {
		proc = cur;
	} else {
		proc = proc_lookup(pid);
		if(!proc) {
			return -ESRCH;
		}

		synchronized(&proc_list_lock) {
			if(proc->pgrp->session != cur->pgrp->session) {
				return -EPERM;
			}
		}
	}

	sync_scope_acquire(&proc->lock);
	return proc->pgrp->id;
}

int sys_setpgid(pid_t pid, pid_t pgid) {
	proc_t *proc, *cur = cur_proc();
	pgrp_t *pgrp, *newgrp;
	int err;

	if(pgid < 0) {
		return -EINVAL;
	}

	newgrp = pgrp_alloc();

	synchronized(&proc_list_lock) {
		if(pid == 0 || pid == cur->pid) {
			proc = cur;
		} else {
			proc = proc_lookup(pid);
			if(!proc) {
				err = -ESRCH;
				goto out;
			}

			synchronized(&proc_tree_lock) {
				if(proc->parent != cur) {
					err = -ESRCH;
					goto out;
				}
			}
		}

		/*
		 * This process is a session leader and thus may not change
		 * its process group.
		 */
		if(proc->pgrp->session->id == proc->pid) {
			err = -EPERM;
			goto out;
		}

		if(pgid == 0) {
			pgid = proc->pid;
		}

		if(proc->pgrp->id == pgid) {
			err = 0;
			goto out;
		}

		pgrp = pgrp_lookup(pgid);
		if(pgrp) {
			/*
			 * The new process group has to be in the same
			 * session as the old process group
			 */
			if(proc->pgrp->session != pgrp->session) {
				err = -EPERM;
				goto out;
			}
		} else {
			if(pgid != proc->pid) {
				err = -EPERM;
				goto out;
			}

			pgrp_setup(newgrp, proc->pid, proc->pgrp->session);
			pgrp = newgrp;
		}

		err = pgrp_enter(proc, pgrp);
		if(pgrp == newgrp) {
			if(err) {
				hashtab_remove(&pgrp_list, pgrp->id,
					&pgrp->node);
			} else {
				newgrp = NULL;
			}
		}
	}

out:
	if(newgrp != NULL) {
		pgrp_free(newgrp);
	}

	return err;
}

int sys_getuid32(void) {
	proc_t *proc = cur_proc();
	sync_scope_acquire(&proc->id_lock);
	return proc->uid;
}

int sys_geteuid32(void) {
	proc_t *proc = cur_proc();
	sync_scope_acquire(&proc->id_lock);
	return proc->euid;
}

int sys_getgid32(void) {
	proc_t *proc = cur_proc();
	sync_scope_acquire(&proc->id_lock);
	return proc->gid;
}

int sys_getegid32(void) {
	proc_t *proc = cur_proc();
	sync_scope_acquire(&proc->id_lock);
	return proc->egid;
}

int sys_setuid32(uid_t id) {
	proc_t *c = cur_proc();
	int ret = 0;

	synchronized(&c->id_lock) {
		if(c->euid == UID_ROOT) {
			c->uid = id;
			c->euid = id;
			c->suid = id;
		} else if(c->uid == id || c->suid == id) {
			c->euid = id;
		} else {
			ret = -EPERM;
		}
	}

	return ret;
}

int sys_setgid32(gid_t id) {
	proc_t *c = cur_proc();
	int ret = 0;

	synchronized(&c->id_lock) {
		if(c->egid == UID_ROOT) {
			c->gid = id;
			c->egid = id;
			c->sgid = id;
		} else if(c->gid == id || c->sgid == id) {
			c->egid = id;
		} else {
			ret = -EPERM;
		}
	}

	return ret;
}

int sys_setresgid32(gid_t rgid, gid_t egid, gid_t sgid) {
	proc_t *proc = cur_proc();

	sync_scope_acquire(&proc->id_lock);
	if(proc->egid != UID_ROOT) {
		/*
		 * "Unprivileged user processes may change the real [G]ID,
		 * effective [G]ID, and saved set-[group]-ID, each to one of:
		 * the current real [G]ID, the current effective UID or the
		 * current saved set-[group]-ID."
		 */
		if((rgid != (gid_t)-1 && rgid != proc->gid &&
			rgid != proc->egid && rgid != proc->sgid) ||
		   (egid != (gid_t)-1 && egid != proc->gid &&
		   	egid != proc->egid && egid != proc->sgid) ||
		   (sgid != (gid_t)-1 && sgid != proc->gid &&
		   	sgid != proc->egid && sgid != proc->sgid))
		{
			return -EPERM;
		}
	}

	if(rgid != (gid_t)-1) {
		proc->gid = rgid;
	}
	if(egid != (gid_t)-1) {
		proc->egid = egid;
	}
	if(sgid != (gid_t)-1) {
		proc->sgid = sgid;
	}

	return 0;
}

int sys_setgroups32(int size, gid_t *list) {
	(void) list;

	if(size < 0) {
		return -EINVAL;
	} else if(size == 0) {
		return 0;
	}

	/*
	 * TODO temporary. setgroups32 is not supported, but if size == 1, then
	 * a setgid() will usually follow and everything is fine.
	 */
	if(size == 1) {
		return 0;
	} else {
		kprintf("[proc] warning: setgroups32: %d\n", size);
		return -ENOTSUP;
	}
}

int sys_getgroups32(int size, gid_t *list) {
	(void) size;
	(void) list;
	return 0;
}

void kproc_add_thread(thread_t *thr) {
	sync_scope_acquire(&kernel_proc.lock);
	proc_add_thread(&kernel_proc, thr);
}

void __init proc_spawn_init(void) {
	session_t *sess;
	thread_t *thread;
	proc_t *proc;
	pgrp_t *grp;
	int err;

	err = proc_new(NULL, INITPROC_PID, &proc);
	if(err) {
		goto err;
	}

	/*
	 * Allocate a new thread.
	 */
	err = thread_new(INITPROC_PID, &thread);
	if(err) {
		goto err;
	}

	hashtab_set(&proc_list, proc->pid, &proc->node_proc);
	synchronized(&proc->lock) {
		proc_add_thread(proc, thread);
	}

	/*
	 * Allocate a session and a pgrp for the init process.
	 */
	sess = session_alloc(proc->pid);
	grp = pgrp_alloc();

	synchronized(&proc_list_lock) {
		pgrp_setup(grp, proc->pid, sess);
		err = pgrp_enter(proc, grp);
		assert(!err);
	}

	init_process = proc;
	sched_add_thread(thread);

	/*
	 * user_main will actually load /bin/init.
	 */
	return;

err:
	kpanic("[proc] could not spawn init-processs");
}

void __init init_proc(void) {
	hashtab_alloc(&pgrp_list, 64, VM_WAIT);
	hashtab_alloc(&proc_list, 128, VM_WAIT);

	proc_local_size = local_size(PROC_LOCAL, proc_local_t);
	proc_init(&kernel_proc, &vm_kern_vas, NULL, -1, NULL);

	kproc_add_thread(&boot_thread);
}

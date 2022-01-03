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
#include <kern/signal.h>
#include <kern/proc.h>
#include <kern/sync.h>
#include <kern/user.h>
#include <lib/bitset.h>
#include <lib/list.h>
#include <lib/string.h>

#define SIGPENDING(proc, thr) \
	(((proc)->npending && sigpending(&(proc)->pending, &(thr)->mask)) || \
	((thr)->npending && sigpending(&(thr)->pending, &(thr)->mask)))

typedef struct sigproc {
	sync_t lock;

	sigaction_t sigacts[NSIG];
	sigset_t pending;
	size_t npending;
} sigproc_t;

typedef struct sigthr {
	sigset_t pending;

	/*
	 * The mask is protected by sigproc->lock.
	 */
	sigset_t mask;
	size_t npending;

	stack_t stack;

	size_t nesting_lvl;
	size_t stack_lvl;
} sigthr_t;

#define SIGACT_CORE	SIGACT_TERM
typedef enum sig_act {
	SIGACT_TERM,
	SIGACT_IGN,
	SIGACT_STOP,
	SIGACT_CONT,
} sig_act_t;

static int sigproc_init(proc_t *proc);
static int sigproc_fork(proc_t *dst_proc, proc_t *src_proc);
static void sigproc_exec(proc_t *proc);
static void sigproc_exit(proc_t *proc);
static int sigthr_init(thread_t *thread);
static define_pls(signal_pls) = {
	.size = sizeof(sigproc_t),
	.init = sigproc_init,
	.fork = sigproc_fork,
	.exec = sigproc_exec,
	.exit = sigproc_exit,
};
static define_tls(signal_tls) = {
	.size = sizeof(sigthr_t),
	.init = sigthr_init,
	.exit = NULL,
};

static inline sigproc_t *sigproc(proc_t *proc) {
	if(proc == NULL) {
		proc = cur_proc();
	}
	return pls_get_proc(proc, &signal_pls);
}

static inline sigthr_t *sigthr(thread_t *thr) {
	if(thr == NULL) {
		thr = cur_thread();
	}
	return tls_thread_get(thr, &signal_tls);
}

bool signal_is_masked(int sig) {
	sigthr_t *thr = sigthr(NULL);
	return sigblocked(&thr->mask, sig);
}

bool signal_is_ignored(int sig) {
	sigproc_t *sigp = sigproc(NULL);

	if(signal_is_masked(sig)) {
		return true;
	} else {
		sync_scope_acquire(&sigp->lock);
		return sigp->sigacts[sig].sa_handler == SIG_IGN;
	}
}

/**
 * @brief Search the first signal that is set in a sigset.
 *
 * @return  0   No signal was set in the sigset.
 *			> 0 The first signal that was set.
 */
static int sigset_ffs(sigset_t *set) {
	/*
	 * Prefer SIGSEGV and SIGKILL.
	 */
	if(sigblocked(set, SIGSEGV)) {
		return SIGSEGV;
	} else if(sigblocked(set, SIGKILL)) {
		return SIGKILL;
	}

	for(int i = 0; i < NSIG; i++) {
		if(sigblocked(set, i + 1)) {
			return i + 1;
		}
	}

	return 0;
}

static bool sigperm(proc_t *proc, int flags) {
	proc_t *cur = cur_proc();
	uid_t uid, euid;

	/*
	 * The kernel can send a signal to everyone.
	 */
	if(flags & SIG_KERN) {
		return true;
	}

	synchronized(&cur->id_lock) {
		uid = cur->uid;
		euid = cur->euid;
	}

	/*
	 * root can do anything.
	 */
	if(uid == UID_ROOT || euid == UID_ROOT) {
		return true;
	}

	sync_scope_acquire(&proc->id_lock);

	/*
	 * "the real or effective user ID of the sending process shall match
	 * the real or saved set-user-ID of the receiving process."
	 * -- pubs.opengroup.org
	 */
	return proc->uid == uid || proc->suid == uid ||
			proc->uid == euid || proc->suid == uid;
}

/**
 * @brief Get the default action of a signal.
 */
static sig_act_t sigdefault(int sig) {
	switch(sig) {
	case SIGHUP: case SIGINT: case SIGKILL: case SIGPIPE:
	case SIGALRM: case SIGTERM: case SIGUSR1: case SIGUSR2:
		return SIGACT_TERM;
	case SIGCHLD:
		return SIGACT_IGN;
	case SIGQUIT: case SIGILL: case SIGABRT:
	case SIGFPE: case SIGSEGV: case SIGBUS:
	case SIGSYS: case SIGXCPU: case SIGXFSZ:
		return SIGACT_CORE;
	case SIGSTOP: case SIGTSTP: case SIGTTIN:
	case SIGTTOU:
		return SIGACT_STOP;
	case SIGCONT:
		return SIGACT_CONT;
	default:
		return SIGACT_IGN;
	}
}

/**
 * @brief Check whether a signal may be ignored.
 */
static bool sigmayign(int sig) {
	return sig != SIGSEGV && sig != SIGKILL && sig != SIGSTOP;
}

static void sigmask_fix(sigset_t *set) {
	/*
	 * SIGKILL, SIGSTOP and SIGSEGV cannot be blocked.
	 */
	sigunblock(set, SIGKILL);
	sigunblock(set, SIGSTOP);
	sigunblock(set, SIGSEGV);
}

/**
 * @brief Choose a thread for handling a signal.
 */
static thread_t *sig_choose_thr(proc_t *proc, int sig) {
	thread_t *cur, *thread = NULL;

	sync_assert(&proc->lock);
	sync_assert(&sigproc(proc)->lock);

	/*
	 * Prefer cur_thread if the signal is not blocked.
	 */
	cur = cur_thread();
	if(proc == cur->proc && !sigblocked(&sigthr(cur)->mask, sig)) {
		return cur;
	}

	/*
	 * Search for threads, which do not block that signal.
	 */
	foreach(cur, &proc->threads) {
		sigthr_t *cur_sig = sigthr(cur);

		if(!sigblocked(&cur_sig->mask, sig) || thread == NULL) {
			thread = cur;
			break;
		}
	}

	assert(thread);
	return thread;
}

static int kern_signal(proc_t *proc, thread_t *thread, int sig, int flags) {
	sigproc_t *sigp = sigproc(proc);
	sigset_t *pending = NULL;
	sigthr_t *sigt;
	sig_act_t dfl_act;
	void *handler;
	bool intr, restart;

	sync_assert(&proc->lock);
	if(sig < 0 || sig >= NSIG) {
		return -EINVAL;
	}

	if(!sigperm(proc, flags)) {
		return -EACCES;
	}

	/*
	 * Check if signal is 0 after checking for permissions,
	 * because the kill syscall may be used to check if
	 * a process exists and if the current process is
	 * capable of signalling it. On top of that do
	 * not send a signal to a process which already exited.
	 */
	if(sig == 0 || (proc->flags & PROC_EXIT)) {
		return 0;
	}

	sync_acquire(&sigp->lock);
	handler = sigp->sigacts[sig].sa_handler;
	dfl_act = sigdefault(sig);

	/*
	 * Don't send a signal if it is ignored anyway.
	 */
	if(handler == SIG_IGN && sigmayign(sig)) {
		sync_release(&sigp->lock);
		return 0;
	}

	/*
	 * Handle sigstop.
	 */
	if(sig == SIGSTOP || (handler == SIG_DFL && dfl_act == SIGACT_STOP)) {
		/*
		 * Clear any pending SIGCONT signals. Remember that SIGCONT can
		 * only be on the proc's pending set.
		 */
		sigunblock(&sigp->pending, SIGCONT);
		sync_release(&sigp->lock);

		/*
		 * This signal cannot be caught.
		 */
		proc_stop(proc, sig);
		return 0;
	} else if(sig == SIGCONT || (handler == SIG_DFL && dfl_act ==
		SIGACT_CONT))
	{
		/*
		 * Don't return after proc_cont to allow SIGCONT to be handled.
		 */
		proc_cont(proc);
	}

	if(handler == SIG_DFL) {
		switch(dfl_act) {
		/*
		 * STOP / CONT default actions
		 * have been handled by the code
		 * above.
		 */
		case SIGACT_CONT:
		case SIGACT_STOP:
		case SIGACT_IGN:
			sync_release(&sigp->lock);
			return 0;
		default:
			break;
		}
	}

	/*
	 * Never set SIGCONT in a threads pending set. If SIGCONT is just in
	 * the process's set, a subsequent SIGSTOP can remove pending SIGCONT
	 * by just changing the process's pending set and not touch the thread's
	 * pending set.
	 */
	if(thread == NULL || sig == SIGCONT) {
		thread = sig_choose_thr(proc, sig);
		pending = &sigp->pending;
		sigt = sigthr(thread);
	} else {
		sigt = sigthr(thread);
		pending = &sigt->pending;
	}

	if(!sigblocked(pending, sig)) {
		if(pending == &sigp->pending) {
			sigp->npending++;
		} else {
			sigt->npending++;
		}
	}

	sigblock(pending, sig);
	intr = SIGPENDING(sigp, sigt);
	restart = !!(sigp->sigacts[sig].sa_flags & SA_RESTART);
	sync_release(&sigp->lock);

	if(intr) {
		/*
		 * Interrupt the thread, so it can handle the signal. The
		 * real stuff happens in signal_intr(), called by
		 * thread_uret.
		 */
		thread_signal(thread, restart);
	}

	return 0;
}

int kern_tkill(thread_t *thread, int sig, int flags) {
	sync_assert(&thread->proc->lock);
	return kern_signal(thread->proc, thread, sig, flags);
}

int kern_tkill_cur(int sig) {
	thread_t *thr = cur_thread();
	sync_scope_acquire(&thr->proc->lock);
	return kern_tkill(thr, sig, SIG_KERN);
}

int kern_kill(proc_t *proc, int sig, int flags) {
	sync_assert(&proc->lock);
	return kern_signal(proc, NULL, sig, flags);
}

int sys_sigaltstack(stack_t *uss, stack_t *oss) {
	sigthr_t *sigt = sigthr(NULL);
	stack_t ss;
	int err;

	if(oss != NULL) {
		err = copyout(oss, &sigt->stack, sizeof(sigt->stack));
		if(err) {
			return err;
		}
	}

	if(uss == NULL) {
		return 0;
	}

	err = copyin(&ss, uss, sizeof(ss));
	if(err) {
		return err;
	}

	/*
	 * TODO SS_AUTODISARM
	 */
	if(ss.ss_flags & ~(SS_DISABLE)) {
		return -EINVAL;
	}

	/*
	 * The thread is currently running on the altstack and thus
	 * sigaltstack cannot change it.
	 */
	if(sigt->stack.ss_flags & SS_ONSTACK) {
		return -EPERM;
	}

	sigt->stack = ss;
	return 0;
}

int sys_rt_sigaction(int signum, sigaction_t *usract, sigaction_t *oldact,
	size_t size)
{
	proc_t *proc = cur_proc();
	sigproc_t *pls = sigproc(proc);
	sigaction_t act, old;
	int err;

	if(size != sizeof(act.sa_mask)) {
		return -ENOSYS;
	}

	if(signum == SIGKILL || signum == SIGSTOP || signum < 0 ||
		signum > NSIG)
	{
		return -EINVAL;
	}

	if(usract) {
		err = copyin(&act, usract, sizeof(act));
		if(err) {
			return err;
		}

		/*
		 * TODO
		 */
		if(act.sa_flags & SA_SIGINFO) {
			return -EINVAL;
		}

		/*
		 * The SA_RESTORER flag is simply ignored.
		 */
		if(act.sa_flags & ~(SA_NODEFER | SA_RESTART | SA_NOCLDWAIT |
			SA_SIGINFO | SA_ONSTACK | SA_RESETHAND | SA_RESTORER))
		{
			return -EINVAL;
		}

		if(signum == SIGCHLD) {
			if(act.sa_flags & SA_NOCLDWAIT) {
				proc_autoreap_enable(proc);
			} else {
				proc_autoreap_disable(proc);
			}
		}
	}

	synchronized(&pls->lock) {
		if(oldact) {
			old = pls->sigacts[signum];
		}
		if(usract) {
			pls->sigacts[signum] = act;
		}
	}

	if(oldact) {
		err = copyout(oldact, &old, sizeof(old));
		if(err) {
			return err;
		}
	}

	return 0;
}

int kern_sigprocmask(int how, sigset_t *set, sigset_t *oldset) {
	thread_t *thread = cur_thread();
	sigproc_t *pls = sigproc(thread->proc);
	sigthr_t *tls = sigthr(thread);
	bool pending = false;

	sigmask_fix(&tls->mask);
	synchronized(&pls->lock) {
		/*
		 * Save the old sigset.
		 */
		if(oldset) {
			*oldset = tls->mask;
		}

		/*
		 * Update the mask based on 'how'.
		 */
		if(set) {
			switch(how) {
			case SIG_BLOCK:
				sigset_or(&tls->mask, set);
				break;
			case SIG_SETMASK:
				memcpy(&tls->mask, set, sizeof(sigset_t));
				break;
			case SIG_UNBLOCK:
				sigset_nand(&tls->mask, set);
				break;
			default:
				return -EINVAL;
			}

			if(how == SIG_UNBLOCK || how == SIG_SETMASK) {
				pending = SIGPENDING(pls, tls);
			}
		}
	}

	if(pending) {
		/*
		 * TODO How to determine whether -ERESTART or not?
		 * maybe: sigp->restart (sigmask) and do sth like:
		 * restart = (pending & sigp->restart) ? true : false
		 */
		thread_signal(thread, true);
	}

	return 0;
}

int sys_rt_sigprocmask(int how, sigset_t *usr_set, sigset_t *usr_oldset,
	size_t size)
{
	sigset_t set, *setp = NULL, oldset;
	int err;

	if(size != sizeof(sigset_t)) {
		return -ENOSYS;
	}

	if(usr_set) {
		err = copyin(&set, usr_set, sizeof(sigset_t));
		if(err) {
			return err;
		}

		setp = &set;
	}

	err = kern_sigprocmask(how, setp, &oldset);
	if(!err && usr_oldset) {
		err = copyout(usr_oldset, &oldset, sizeof(sigset_t));
	}

	return err;
}

int sys_tkill(int tid, int sig) {
	proc_t *proc = cur_proc();
	thread_t *thr;

	if(tid <= 0) {
		return -EINVAL;
	}

	if(cur_thread()->tid == tid) {
		return kern_tkill_cur(sig);
	}

	synchronized(&proc->lock) {
		foreach(thr, &proc->threads) {
			if(thr->tid == tid) {
				return kern_tkill(thr, sig, 0);
			}
		}
	}

	return -ESRCH;
}

int sys_kill(pid_t pid, int sig) {
	int err;

	if(pid > 0) {
		/*
		 * Send a signal to a single process.
		 */
		err = proc_kill(pid, sig, 0);
	} else if(pid == 0 || pid < -1) {
		err = pgrp_kill(ABS(pid), sig, 0);
	} else {
		/*
		 * Currently not supported: 'sig is sent to every process for
		 * which the calling process has permission to send signals'
		 * -- http://man7.org
		 */
		kprintf("[signal] TODO implement\n");
		err = -EINVAL;
	}

	return err;
}

void signal_set_mask(sigset_t *set) {
	thread_t *thread = cur_thread();
	sigthr_t *sigt = sigthr(thread);
	sigproc_t *sigp = sigproc(thread->proc);
	bool intr;

	sigmask_fix(set);
	sync_acquire(&sigp->lock);
	sigt->mask = *set;
	intr = SIGPENDING(sigp, sigt);
	sync_release(&sigp->lock);

	if(intr) {
		/*
		 * TODO How to determine whether -ERESTART or not?
		 * maybe: sigp->restart (sigmask) and do sth like:
		 * restart = (pending & sigp->restart) ? true : false
		 */
		thread_signal(thread, true);
	}
}

static int sigproc_init(proc_t *proc) {
	sigproc_t *sig = sigproc(proc);

	/*
	 * TODO maybe consider SPINLOCK here.
	 */
	sync_init(&sig->lock, SYNC_MUTEX);

	/*
	 * Initialize the signal handlers here when initializing
	 * sigproc from the init process. Other processes have
	 * to be forked from a thread and thus sigproc_fork
	 * will initialize the sigacts.
	 */
	if(!proc_was_forked(proc)) {
		for(size_t i = 0; i < NSIG; i++) {
			sig->sigacts[i].sa_handler = SIG_DFL;
		}
	}

	return 0;
}

static int sigproc_fork(proc_t *dst_proc, proc_t *src_proc) {
	sigproc_t *dst = sigproc(dst_proc), *src = sigproc(src_proc);
	sigthr_t *dst_thr, *src_thr = sigthr(NULL);

	assert(src_proc == cur_proc());
	assert(list_length(&dst_proc->threads) == 1);

	dst_thr = sigthr(list_first(&dst_proc->threads));

	sync_scope_acquire(&src->lock);
	dst_thr->mask = src_thr->mask;
	for(size_t i = 0; i < NSIG; i++) {
		memcpy(&dst->sigacts[i], &src->sigacts[i], sizeof(sigaction_t));
	}

	return 0;
}

static void sigproc_exec(proc_t *proc) {
	sigproc_t *pls = sigproc(proc);
	sigthr_t *tls = sigthr(NULL);

	sync_scope_acquire(&pls->lock);

	/*
	 * Clear any pending signals.
	 */
	memset(&tls->pending, 0x00, sizeof(tls->pending));
	memset(&pls->pending, 0x00, sizeof(pls->pending));
	tls->npending = 0;
	pls->npending = 0;

	for(size_t i = 0; i < NSIG; i++) {
		/*
		 * The dispositions of ignored signals are left unchanged.
		 */
		if(pls->sigacts[i].sa_handler != SIG_IGN) {
			pls->sigacts[i].sa_handler = SIG_DFL;
		}

		pls->sigacts[i].sa_flags = 0;
	}
}

static void sigproc_exit(proc_t *proc) {
	sigproc_t *sig = sigproc(proc);
	sync_destroy(&sig->lock);
}

static int sigthr_init(thread_t *thread) {
	sigthr_t *sig = sigthr(thread);

	memset(sig, 0, sizeof(*sig));
	sig->stack.ss_flags = SS_DISABLE;

	return 0;
}

static int signext(sigproc_t *sigp, sigthr_t *sigt, sigaction_t **act) {
	sigset_t sigset;
	int sig;

	sync_assert(&sigp->lock);
	if(SIGPENDING(sigp, sigt) == false) {
		return 0;
	}

	sigset = sigt->pending;

	/*
	 * Combine the thread's pending sigset and
	 * the proc's pending sigset.
	 */
	sigset_or(&sigset, &sigp->pending);

	/*
	 * Remove the masked signals.
	 */
	sigset_nand(&sigset, &sigt->mask);

	sig = sigset_ffs(&sigset);
	if(sig > 0) {
		sigset_t *set;

		/*
		 * Clear the signal from the pending sigseg.
		 */
		if(sigblocked(&sigp->pending, sig)) {
			set = &sigp->pending;
			sigp->npending--;
		} else {
			set = &sigt->pending;
			sigt->npending++;
		}

		sigunblock(set, sig);
		*act = &sigp->sigacts[sig];
	}

	return sig;
}

static inline void signal_cleanup(sigthr_t *sigt) {
	assert(sigt->nesting_lvl > 0);
	if(sigt->stack_lvl == sigt->nesting_lvl) {
		sigt->stack.ss_flags &= ~SS_ONSTACK;
		sigt->stack_lvl = 0;
	}

	sigt->nesting_lvl--;
}

void signal_intr(void) {
	thread_t *thr = cur_thread();
	sigthr_t *sigt = sigthr(thr);
	sigproc_t *sigp = sigproc(thr->proc);
	proc_t *proc = thr->proc;
	sigaction_t *act, act_cpy;
	stack_t *stack;
	sigset_t old;
	int sig, err;

	sync_acquire(&sigp->lock);
	sig = signext(sigp, sigt, &act);
	if(sig == 0) {
		sync_release(&sigp->lock);
		return;
	} else if(sig == SIGKILL) {
		sync_release(&sigp->lock);

		/*
		 * SIGKILL cannot be handled.
		 */
		kern_exitproc(0, sig);
		return;
	}

	/*
	 * Handler might have changed since kern_tkill -> have to
	 * do similar checks like in kern_tkill over here.
	 */
	if(act->sa_handler == SIG_IGN && sig != SIGSEGV) {
		sync_release(&sigp->lock);

		/*
		 * Ignore the signal.
		 */
		return;
	} else if(act->sa_handler == SIG_DFL) {
		sig_act_t act = sigdefault(sig);

		if(act == SIGACT_TERM) {
			sync_release(&sigp->lock);

			/*
			 * Bye, bye world.
			 */
			kern_exitproc(0, sig);
		} else if(act == SIGACT_STOP) {
			/*
			 * TODO is this safe?
			 */
			sigunblock(&sigp->pending, SIGCONT);
			sync_release(&sigp->lock);

			synchronized(&proc->lock) {
				proc_stop(proc, sig);
			}
		} else if(act == SIGACT_CONT) {
			sync_release(&sigp->lock);

			/*
			 * Is this actually needed? The thread would not be
			 * running if the process would currently be stopped.
			 */
			sync_scope_acquire(&proc->lock);
			proc_cont(proc);
		}

		return;
	}

	/*
	 * Save old signal mask.
	 */
	old = sigt->mask;

	/*
	 * Block some signals.
	 */
	sigset_or(&sigt->mask, &act->sa_mask);
	if(!(act->sa_flags & SA_NODEFER)) {
		sigblock(&sigt->mask, sig);
	}
	sigmask_fix(&sigt->mask);

	act_cpy = *act;

	/*
	 * TODO In addition, if this flag is set, sigaction() behaves as if the
	 * SA_NODEFER flag were also set.
	 */
	if((act->sa_flags & SA_RESETHAND) && sig != SIGILL && sig != SIGTRAP) {
		act->sa_handler = SIG_DFL;
		act->sa_flags &= ~SA_SIGINFO;
	}

	sync_release(&sigp->lock);
	sigt->nesting_lvl++;

	/*
	 * Handle alternative signal stack. Do not switch the stack if
	 * the thread is already on the alternative stack.
	 */
	if((act->sa_flags & SA_ONSTACK) &&
		(sigt->stack.ss_flags & (SS_DISABLE | SS_ONSTACK)) == 0)
	{
		sigt->stack_lvl = sigt->nesting_lvl;
		sigt->stack.ss_flags |= SS_ONSTACK;
		stack = &sigt->stack;
	} else if(sig == SIGSEGV) {
		/*
		 * A sigsegv can only bes handled when an alternate stack
		 * is available.
		 */
		kern_exitproc(0, sig);
	} else {
		stack = NULL;
	}

	err = arch_sig_handle(thr, sig, stack, &act_cpy, &old);
	if(err) {
#if 0
		/*
		 * TODO what to do on error?
		 */
		signal_cleanup(sigt);
		signal_set_mask(&old);
#endif
		kern_exitproc(0, sig);
	}
}

int sys_sigreturn(void) {
	thread_t *thread = cur_thread();
	signal_cleanup(sigthr(thread));
	return arch_sigreturn(thread);
}

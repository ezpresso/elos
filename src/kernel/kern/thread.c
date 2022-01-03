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
#include <kern/sched.h>
#include <kern/cpu.h>
#include <kern/critical.h>
#include <kern/signal.h>
#include <kern/futex.h>
#include <kern/init.h>
#include <kern/user.h>
#include <kern/main.h>
#include <vm/malloc.h>
#include <vm/vmem.h>
#include <lib/bitset.h>
#include <lib/string.h>
#include <sys/sched.h>

static size_t thread_local_size = 0;
static bset_t tid_bitset;
static sync_t tid_lock = SYNC_INIT(MUTEX);

__initdata thread_t boot_thread = {
	.prio = SCHED_KERNEL,
	.state = THREAD_RUNNING,
	.tid = KTHREAD_TID,
	.proc = &kernel_proc,
};

static void kthread_entry(void) {
	thread_t *thread = cur_thread();
	kern_exit(thread->kfunc(thread->karg));
}

static void thread_init(thread_t *thread, pid_t tid) {
	list_node_init(thread, &thread->sched_node);
	list_node_init(thread, &thread->proc_node);
	thread->prio = SCHED_NORMAL;
	thread->sflags = 0;
	thread->intr = 0;
	thread->flags = 0;
	thread->state = THREAD_SPAWNED;
	thread->numlock = 0;
	thread->tid = tid;
	thread->onfault = NULL;
	thread->proc = NULL;
	thread->trapframe = NULL;

	thread->set_child_tid = NULL;
	thread->clear_child_tid = NULL;

	arch_thread_init(thread);
}

static thread_t *thread_alloc(pid_t tid) {
	size_t size;
	thread_t *thread;

	/*
	 * Kernel threads currently have no tls.
	 */
	size = sizeof(thread_t);
	if(tid != KTHREAD_TID) {
		size += thread_local_size;
	}

	/*
	 * Allocate space for thread, thread-local storage
	 * and a new kernel stack.
	 */
	thread = kmalloc(size, VM_WAIT);

	/*
 	* TODO add a configuration variable
 	*/
#if 0
	thread->kstack = kmalloc(THREAD_KSTACK, VM_WAIT);
#else
	vm_vaddr_t stack = vmem_alloc(THREAD_KSTACK + PAGE_SZ, VM_WAIT);
	thread->kstack = vmem_back(stack + PAGE_SZ, THREAD_KSTACK, VM_WAIT);
#endif

	if(tid == KTHREAD_TID) {
		thread->tls = NULL;
	} else {
		thread->tls = (void *)thread + sizeof(*thread);
	}

	thread_init(thread, tid);
	return thread;
}

int thread_new(pid_t tid, thread_t **thrp) {
	thread_t *thread;
	int err;

	thread = thread_alloc(tid);
	err = tls_call_err(thread, init);
	if(err) {
		thread_free(thread);
	} else {
		*thrp = thread;
	}

	return err;
}

void thread_free(thread_t *thread) {
	/*
	 * proc->lock is not required in this case (not using proc_test_flags
	 * here because it asserts that the lock is acquired).
	 */
	if(thread->proc && (thread->proc->flags & PROC_FREE)) {
		proc_exit_final(thread->proc);
	}

	arch_thread_exit(thread);
	list_node_destroy(&thread->sched_node);
	list_node_destroy(&thread->proc_node);

	if(unlikely(thread == &boot_thread)) {
		init_free();
	} else {
		if(thread->tls) {
			tls_call(thread, exit);
		}

#if 0
		kfree(thread->kstack);
#else
		vmem_unback(thread->kstack, THREAD_KSTACK);
		vmem_free((vm_vaddr_t)thread->kstack - PAGE_SZ,
			THREAD_KSTACK + PAGE_SZ);
#endif
		kfree(thread);
	}
}

void tid_free(pid_t tid) {
	assert(tid < THREAD_MAX);
	assert(tid != 0);
	assert(tid != 1);

	sync_scope_acquire(&tid_lock);
	bset_free_bit(&tid_bitset, tid);
}

pid_t tid_alloc(void) {
	sync_scope_acquire(&tid_lock);
	return bset_alloc_bit(&tid_bitset);
}

thread_t *kthread_alloc(int (*func) (void *), void *arg) {
	thread_t *thread;

	thread = thread_alloc(KTHREAD_TID);
	thread->kfunc = func;
	thread->karg = arg;
	arch_kthread_setup(thread, (uintptr_t)kthread_entry);
	kproc_add_thread(thread);

	return thread;
}

thread_t *kthread_spawn_prio(int (*func) (void *), void *arg, uint8_t prio) {
	thread_t *thread;

	thread = kthread_alloc(func, arg);
	thread->prio = prio;
	sched_add_thread(thread);

	return thread;
}

thread_t *kthread_spawn(int (*func) (void *), void *arg) {
	return kthread_spawn_prio(func, arg, SCHED_KERNEL);
}

static void thread_handle_intr(thread_t *thread, int intr) {
	/*
	 * Check if the thread should be terminated, prior to
	 * anything else, because handling the other software
	 * interrupts would be a waste of time.
	 */
	if(intr & THREAD_KILL) {
		kern_exit(0);
		notreached();
	}

	if(intr & THREAD_PROC) {
		proc_t *proc = thread->proc;

		synchronized(&proc->lock) {
			int flags = proc->flags;

			if(proc_test_flags(proc, PROC_ST)) {
				assert(proc->st_thread != thread);

				/*
				 * PROC_ST_KILL will issue thread_kill instead
				 * of thread_intr_proc
				 */
				assert(proc->st_mode == PROC_ST_WAIT);

				/*
				 * Tell the single-thread thread that this
				 * thread is waiting now.
				 */
				proc->st_waiting++;
				proc_wakeup_st_waiter(proc);

				while((flags = proc->flags) & PROC_ST &&
					proc->st_mode == PROC_ST_WAIT)
				{
					sync_release(&proc->lock);
					kern_wait(&proc->flags, flags, 0);
					sync_acquire(&proc->lock);
				}

				proc->st_waiting--;
			}

			if(proc_test_flags(proc, PROC_STOP)) {
				while((flags = proc->flags) & PROC_STOP) {
					sync_release(&proc->lock);
					kern_wait(&proc->flags, flags, 0);
					sync_acquire(&proc->lock);
				}
			}
		}
	}

	if(intr & THREAD_SIGNAL) {
		signal_intr();
	}
}

bool thread_interrupted(void) {
	return !!(cur_thread()->sflags & THREAD_INTERRUPTED);
}

void thread_kill(thread_t *thread) {
	assert(thread != cur_thread());
	sched_interrupt(thread, SCHED_KERNEL, THREAD_KILL, 0);
}

void thread_signal(thread_t *thread, bool restart) {
	sched_interrupt(thread, SCHED_SIGNAL, THREAD_SIGNAL,
		restart ? THREAD_RESTARTSYS : 0);
}

void thread_intr_proc(thread_t *thread) {
	sched_interrupt(thread, SCHED_SIGNAL, THREAD_PROC, 0);
}

void thread_uret(void) {
	thread_t *thread = cur_thread();
	int intr;

	/*
	 * Interrupts might be disabled, but a critical section
	 * is not intended.
	 */
	assert_not_critsect("[thread] called thread_uret during critical "
		"section");
	while((intr = sched_pending_intr())) {
		/*
		 * Handle software interrupts. This function
		 * might be called with interrupts disabled (mostly
		 * device interrupts), because device interrupts may
		 * call it too, when finished.
		 */
		cpu_intr_set(true);
		thread_handle_intr(thread, intr);

		/*
		 * Disabling interrupts here should not be a problem
		 * even if the function was called with interrupts enabled.
		 */
		cpu_intr_set(false);
	}

	thread->trapframe = NULL;
}

void thread_clear_tid(void) {
	thread_t *thread = cur_thread();
	int *cct, err;

	cct = thread->clear_child_tid;
	if(cct != NULL) {
		int tmp = 0;

		err = copyout_atomic(cct, &tmp, sizeof(tmp));
		if(err == 0) {
			kern_wake(cct, 1, KWAIT_USR);
		}
	}
}

void __noreturn thread_do_exit(void) {
	thread_t *thread = cur_thread();

	/*
	 * An exit during a critical section is fatal.
	 */
	assert_not_critsect("[thread] exit() during critical section");

	thread->state = THREAD_EXIT;
	schedule();
	notreached();
}

void thread_numlock_inc(void) {
	thread_t *cur = cur_thread();

	if(cur && cur->numlock++ == 0) {
		/*
		 * Temporarily increase priority.
		 */
		cur->saved_prio = thread_prio_push(SCHED_LOCK);
	}
}

void thread_numlock_dec(void) {
	thread_t *cur = cur_thread();

	if(cur && --cur->numlock == 0) {
		thread_prio_pop(cur->saved_prio);
	}
}

sched_prio_t thread_prio_push(sched_prio_t prio) {
	thread_t *thread = cur_thread();
	sched_prio_t tmp = thread->prio;

	if(prio < thread->prio) {
		thread->prio = prio;
	}

	return tmp;
}

void thread_prio_pop(sched_prio_t tmp) {
	cur_thread()->prio = tmp;
}

void *tls_get(thread_local_t *l) {
	thread_t *thread = cur_thread();
	assert(thread);
	return tls_thread_get(thread, l);
}

void *tls_thread_get(thread_t *t, thread_local_t *l) {
	assert(t->tls);
	return t->tls + l->off;
}

int __noreturn sys_exit(__unused int code) {
	kern_exit(code);
}

int sys_gettid(void) {
	return cur_thread()->tid;
}

int sys_set_tid_address(int *addr) {
	thread_t *thread = cur_thread();
	thread->clear_child_tid = addr;
	return thread->tid;
}

void thread_fork_ret(void) {
	thread_t *thr;

	sched_postsched();

	thr = cur_thread();
	if(thr->tid == INITPROC_PID) {
		user_main();
	}

	thr->trapframe = NULL;
	if(thread_is_user(thr) && thr->set_child_tid) {
		copyout(thr->set_child_tid, &thr->tid, sizeof(thr->tid));
	}
}

/*
 * Clone is only used for threads sharing fs/vm/files/tgrp/sighands here.
 */
#define CLONE_FLAGS (CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_THREAD|CLONE_SIGHAND)
#include <arch/gdt.h>

int sys_clone(int flags, int stack, pid_t *ptid, struct user_desc *usr_desc,
	pid_t *ctid)
{
	proc_t *proc = cur_proc();
	struct user_desc desc;
	thread_t *thread;
	uintptr_t tls;
	pid_t tid;
	int err;

	if((flags & CLONE_FLAGS) != CLONE_FLAGS) {
		return -EINVAL;
	}

	if(flags & CSIGNAL) {
		return -ENOTSUP;
	}

	if(flags & CLONE_SETTLS) {
		err = copyin(&desc, usr_desc, sizeof(desc));
		if(err) {
			return err;
		}

		/*
		 * TODO Well that's arch specific stuff.
		 */
		if(desc.entry_number != SEG_GS &&
			desc.entry_number != (uint32_t)-1)
		{
			return -EINVAL;
		}
	}

	tid = tid_alloc();
	if(tid < 0) {
		return -EAGAIN;
	}

	err = thread_new(tid, &thread);
	if(err) {
		tid_free(tid);
		return err;
	}

	if(flags & CLONE_PARENT_SETTID) {
		err = copyout(ptid, &tid, sizeof(tid));
		if(err) {
			thread_free(thread);
			tid_free(tid);
			return err;
		}
	}

	if(flags & CLONE_CHILD_SETTID) {
		thread->set_child_tid = ctid;
	} else {
		thread->set_child_tid = NULL;
	}

	if(flags & CLONE_CHILD_CLEARTID) {
		thread->clear_child_tid = ctid;
	} else {
		thread->clear_child_tid = NULL;
	}

	if(flags & CLONE_SETTLS) {
		tls = desc.base_addr;
	} else {
		tls = 0;
	}

	/*
	 * Copy registers etc.
	 */
	arch_thread_fork(thread, cur_thread(), (uintptr_t)stack, tls);

	/*
	 * Add the new thread to the process.
	 */
	synchronized(&proc->lock) {
		proc_add_thread(proc, thread);
		sched_add_thread(thread);
	}

	return thread->tid;
}

void __init init_thread(void) {
	int err;

	err = bset_alloc(&tid_bitset, THREAD_MAX);
	if(err) {
		kpanic("[thread] allocating tid-bitset failed");
	}

	/*
	 * Pid 0 is reserved and pid 1 reserved for
	 * the init process.
	 */
	bset_set(&tid_bitset, 0);
	bset_set(&tid_bitset, 1);

	thread_local_size = local_size(THREAD_LOCAL, thread_local_t);

	/*
	 * Initialize the rest of the boot_thread.
	 */
	list_node_init(&boot_thread, &boot_thread.sched_node);
	list_node_init(&boot_thread, &boot_thread.proc_node);
	arch_thread_init(&boot_thread);
}

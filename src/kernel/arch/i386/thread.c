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
#include <kern/stack.h>
#include <kern/cpu.h>
#include <kern/init.h>
#include <kern/sched.h>
#include <kern/signal.h>
#include <kern/user.h>
#include <kern/fault.h>
#include <kern/syscall.h>
#include <arch/gdt.h>
#include <arch/frame.h>
#include <arch/x86.h>
#include <arch/interrupt.h> /* int_return */
#include <arch/gdt.h>
#include <vm/shpage.h>
#include <sys/syscall.h>

typedef struct sigcallframe {
	uintptr_t ret;
	int sig;
	void *info;
	void *uctxp;
} sigcallframe_t;

/*
 * This is not equal to linux's sigframe.
 */
typedef struct sigframe {
	sigcallframe_t call;
	ucontext_t uctx;

	/*
	 * syscall to be restarted.
	 */
	int syscall;
	uint8_t pad[4];

	fpstate_t fp;
} sigframe_t;

/**
 * @brief The userspace address of the signal trampoline.
 */
static vm_vaddr_t sigret;

/*
 * The sigframe is located at a 16byte boundary + 4, so that the signal
 * handler's stack has a 16byte aligned stack after 'push %ebp'.
 */
ASSERT(ALIGNED(offsetof(sigframe_t, fp) + 0x4, FPU_ALIGN),
	"fpu ctx has to be aligned");

void arch_thread_init(thread_t *thr) {
	arch_thread_t *arch = &thr->arch;
	kstack_t stack;

	if(thr == &boot_thread) {
		return;
	}

	/*
	 * TODO no fpu for kernel-threads
	 */
	fpu_init(&thr->arch);
	assert(thr->arch.fpu);

	stack_init(&stack, thr->kstack, THREAD_KSTACK);
	arch->kern_esp = (uintptr_t)stack_pointer(&stack);
	arch->cr2 = 0x0;

	/*
	 * Reserve some space for an interrupt stack.
	 */
	thr->trapframe = stack_rsv_type(&stack, trapframe_t);
	stack_pushval(&stack, (uintptr_t)int_return);

	/*
	 * Reserve space for the context.
	 */
	arch->context = stack_rsv_type(&stack, context_t);
	arch->context->edi = 0;
	arch->context->esi = 0;
	arch->context->ebx = 0;
	arch->context->ebp = 0;
	arch->context->eip = (uintptr_t) thread_fork_ret;
	arch->context->eflags = 0;
}

static void arch_thread_setup(thread_t *thr, uintptr_t ip, uintptr_t ustack,
	bool usr)
{
	tf_fake(thr->trapframe, ip, ustack, usr);
}

void arch_uthread_setup(thread_t *thr, uintptr_t ip, uintptr_t ustack) {
	arch_thread_setup(thr, ip, ustack, true);
}

void arch_kthread_setup(thread_t *thr, uintptr_t ip) {
	arch_thread_setup(thr, ip, 0, false);
}

void arch_thread_fork(thread_t *dst, thread_t *src, uintptr_t stack,
	uintptr_t tls)
{
	/*
	 * Copy the syscall trapframe.
	 */
	*dst->trapframe = *src->trapframe;

	/*
	 * TODO could add an arch independent value (uintptr_t tls) in
	 * thread_t instead of arch_thread_t!!!
	 */
	if(dst->proc == src->proc) {
		/*
		 * When the two threads share the virtual address space
		 * they need different addresses for tls.
		 */
		dst->arch.gs_base = tls;
	} else {
		/*
		 * When forking a process, dst and src have the same
		 * address for tls.
		 */
		dst->arch.gs_base = src->arch.gs_base;
	}

	if(stack) {
		dst->trapframe->useresp = stack;
	}

	tf_set_retval(dst->trapframe, 0);
	fpu_clone(dst->arch.fpu, src->arch.fpu);
}

void arch_thread_exit(__unused thread_t *thread) {
	return;
}

void arch_thread_switch(thread_t *to, thread_t *from) {
	if(!thread_is_kern(from) && from->state != THREAD_EXIT) {
		fpu_save(from->arch.fpu);
	}

	if(!thread_is_kern(to)) {
		fpu_restore(to->arch.fpu);
	}

	setgs(to->arch.gs_base);
	cpu_set_kernel_stack(to->arch.kern_esp);
	context_switch(&from->arch.context, to->arch.context);
}

static int copyout_uctx(thread_t *thread, ucontext_t *uctx, trapframe_t *tf,
	sigset_t *sigset, fpstate_t *fp)
{
	mayfault(error) {
		uctx->uc_flags = UC_FP_XSTATE;

		/*
		 * The beginning of sigcontext and trapframe are the same.
		 */
		memcpy(&uctx->uc_mcontext, tf, sizeof(*tf));
		memcpy(&uctx->uc_sigmask, sigset, sizeof(*sigset));
		uctx->uc_mcontext.fpstate = fp;
		uctx->uc_mcontext.oldmask = 0x0; // TODO sigset->sig[0]; /* ? */
		uctx->uc_mcontext.cr2 = thread->arch.cr2;

		/* TODO */
		uctx->uc_stack.ss_sp = NULL;
		uctx->uc_stack.ss_flags = 0;
		uctx->uc_stack.ss_size = 0;
	}

	return 0;

error:
	return -EFAULT;
}

int arch_sig_handle(thread_t *thr, int sig, stack_t *altstack,
	sigaction_t *sigact, sigset_t *mask)
{
	trapframe_t *tf = thr->trapframe;
	sigcallframe_t call;
	sigframe_t *frame;
	void *stackaddr;
	kstack_t stack;
	int err;

	if(thr->tid == -1) {
		kpanic("kernel thread cannot handle signals");
	}

	if(altstack) {
		stackaddr = altstack->ss_sp + altstack->ss_size;
	} else {
		stackaddr = (void *)tf->useresp;
	}

	stack_init(&stack, stackaddr, 0);

	/*
	 * The i386 ABI expects 16 byte alignment.
	 */
	stack_rsv_align(&stack, sizeof(*frame) + 16, 16);

	/*
	 * Increment stack pointer by 4, so that a future push %ebp will
	 * result in 16 bit alignment.
	 */
	stack_pop(&stack, 4);

	frame = stack_pointer(&stack);
	kassert(PTR_ALIGNED((void *)frame - 4, 16), "[thread] arch signal "
		"handler: stack alignment problem: 0x%p", frame);

	/*
	 * Copy fpu context to userspace.
	 */
	err = copyout_fpu(&frame->fp);
	if(err) {
		return err;
	}

	err = copyout_uctx(thr, &frame->uctx, tf, mask, &frame->fp);
	if(err) {
		return err;
	}

	call.sig = sig;
	call.ret = sigret;
	assert(!F_ISSET(sigact->sa_flags, SA_SIGINFO));

	/* if(sigact->sa_flags & SA_SIGINFO) {
		TODO
	} else */ {
		call.info = NULL,
		call.uctxp = NULL;
	}

	err = copyout(&frame->call, &call, sizeof(call));
	if(err) {
		return err;
	}

	err = copyout(&frame->syscall, &thr->syscall, sizeof(thr->syscall));
	if(err) {
		return err;
	}

	tf->useresp = stack_addr(&stack);
	tf->eip = (uintptr_t) sigact->sa_handler;

	/*
	 * Support applications compiled with regparam.
	 */
	tf->eax = sig;

	return 0;
}

int arch_sigreturn(thread_t *thr) {
	trapframe_t *tf = thr->trapframe;
	sigframe_t *frame;
	sigset_t set;
	int err;

	/*
	 * TODO
	 * sigreturn is not a normal syscall. The return
	 * value is ignored...
	 */

	/*
	 * -4 because the 'ret' instruction popped of the return
	 * address.
	 */
	frame = (void *)tf->useresp - 4;

	/*
	 * Restore fpu context.
	 */
	err = copyin_fpu(&frame->fp);
	if(err) {
		return err;
	}

	/*
	 * Restore registers.
	 */
	err = copyin(tf, &frame->uctx.uc_mcontext, sizeof(*tf));
	if(err) {
		return err;
	}

	/*
	 * Make sure that the user did not do anything evil with the
	 * registers. The user might have loaded the kernel %cs or it
	 * could have changed IOPL in eflags.
	 */
	tf->fs = FS_SEL;
	tf->gs = GS_SEL;
	tf->cs = UCODE_SEL;
	tf->es = UDATA_SEL;
	tf->ds = UDATA_SEL;
	tf->ss = UDATA_SEL;

	/*
	 * Don't allow the user to set IOPL, virtual interrupt stuff,
	 * interrupt flag and virtual x86 mode.
	 */
	tf->eflags &= EFL_CF | EFL_PF | EFL_AF | EFL_ZF | EFL_SF | EFL_TF |
		EFL_DF | EFL_OF | EFL_NT | EFL_RF | EFL_AC | EFL_ID;
	tf->eflags |= EFL_IF;

	/*
	 * Restore the signal mask.
	 */
	err = copyin(&set, &frame->uctx.uc_sigmask, sizeof(set));
	if(err) {
		return err;
	}

	signal_set_mask(&set);

	/*
	 * Restart the syscall.
	 */
	if(tf_get_retval(tf) == -ERESTART) {
		int sysc;

		/*
		 * The syscall return value and syscall both use eax and because
		 * the retval (i.e. -ERESTART) is currently in eax the syscall
		 * number has to be restored.
		 */
		err = copyin(&sysc, &frame->syscall, sizeof(sysc));
		if(err) {
			return err;
		}

		tf_set_syscall_num(tf, sysc);

		/*
		 * The correct syscall arguments should be still inside
		 * the restored trapframe.
		 */
		syscall();
	}

	return 0;
}

int sys_set_thread_area(struct user_desc *udesc) {
	thread_t *thread = cur_thread();
	struct user_desc desc;
	int err;

	err = copyin(&desc, udesc, sizeof(desc));
	if(err) {
		return err;
	}

	if((int)desc.entry_number != -1 && desc.entry_number != SEG_GS) {
		return -EINVAL;
	}

	thread->arch.gs_base = desc.base_addr;
	setgs(thread->arch.gs_base);

	desc.entry_number = SEG_GS;

	return copyout(udesc, &desc, sizeof(desc));
}

static __init int arch_signal_init(void) {
	static const struct {
		uint8_t movl;
		uint32_t val;
		uint16_t int80;
		uint8_t pad;
	} __packed code = {
		0xb8, /* movl $SYS_sigreturn, %eax */
		SYS_sigreturn,
		0x80cd, /* int $0x80 */
		0x0,
	};
	void *shared;

	/*
	 * Allocate some space in the share page with 4 byte alignment.
	 * Technically it would not be necessery to put this bytecode
	 * into an extra shared page, it could just be pushed onto the
	 * stack when a signal is hanled. However this would enforce
	 * executable stacks.
	 */
	shared = vm_shpage_alloc(sizeof(code), sizeof(uintptr_t));
	memcpy(shared, &code, sizeof(code));

	/*
	 * Retrieve the userspace address of the signal trampoline.
	 */
	sigret = vm_shpage_addr(shared);

	return INIT_OK;
}

late_initcall(arch_signal_init);

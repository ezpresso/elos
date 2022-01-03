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
#include <kern/syscall.h>
#include <kern/proc.h>
#include <kern/sched.h>
#include <kern/exec.h>
#include <kern/signal.h>
#include <kern/time.h>
#include <kern/critical.h>
#include <kern/futex.h>
#include <arch/frame.h>
#include <vfs/sys.h>
#include <net/socket.h>
#include <vm/sys.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <lib/string.h>

struct sysinfo;
struct utsname;
/*
 * TODO move this somewhere else
 */
extern int sys_sysinfo(struct sysinfo *info);
extern int sys_uname(struct utsname *buf);

#define SYSCALL_ENTRY(sys) \
	[SYS_ ## sys] = { .name = # sys, .func = (sys_ ## sys) }

typedef struct syscall {
	void *func;
	char *name;
} syscall_t;

static syscall_t syscalls[] = {
	SYSCALL_ENTRY(sysinfo),
	SYSCALL_ENTRY(uname),

	SYSCALL_ENTRY(prlimit64),
	SYSCALL_ENTRY(getrusage),
	SYSCALL_ENTRY(madvise),

	/*
	 * Threading syscalls
	 */
	SYSCALL_ENTRY(set_thread_area),
	SYSCALL_ENTRY(set_tid_address),
	SYSCALL_ENTRY(gettid),
	SYSCALL_ENTRY(clone),
	SYSCALL_ENTRY(exit_group),
	SYSCALL_ENTRY(exit),
	SYSCALL_ENTRY(futex),

	/*
	 * Process syscalls
	 */
	SYSCALL_ENTRY(fork),
	SYSCALL_ENTRY(vfork),
	SYSCALL_ENTRY(getpid),
	SYSCALL_ENTRY(getppid),
	SYSCALL_ENTRY(setsid),
	SYSCALL_ENTRY(getsid),

	SYSCALL_ENTRY(setuid32),
	SYSCALL_ENTRY(setgid32),

	SYSCALL_ENTRY(getuid32),
	SYSCALL_ENTRY(geteuid32),
	SYSCALL_ENTRY(getgid32),
	SYSCALL_ENTRY(getegid32),
	SYSCALL_ENTRY(getgroups32),
	SYSCALL_ENTRY(setgroups32),
	SYSCALL_ENTRY(setresgid32),

	SYSCALL_ENTRY(getpgid),
	SYSCALL_ENTRY(setpgid),
	SYSCALL_ENTRY(wait4),

	/*
	 * Signal syscalls
	 */
	SYSCALL_ENTRY(rt_sigprocmask),
	SYSCALL_ENTRY(rt_sigaction),
	SYSCALL_ENTRY(kill),
	SYSCALL_ENTRY(tkill),
	SYSCALL_ENTRY(sigreturn),
	SYSCALL_ENTRY(sigaltstack),

	/*
	 * Filesystem syscalls
	 */
	SYSCALL_ENTRY(open),
	SYSCALL_ENTRY(openat),
	SYSCALL_ENTRY(close),
	SYSCALL_ENTRY(fchownat),
	SYSCALL_ENTRY(chown32),
	SYSCALL_ENTRY(lchown32),
	SYSCALL_ENTRY(fchown32),
	SYSCALL_ENTRY(fchmodat),
	SYSCALL_ENTRY(chmod),
	SYSCALL_ENTRY(fchmod),
	SYSCALL_ENTRY(stat64),
	SYSCALL_ENTRY(lstat64),
	SYSCALL_ENTRY(fstat64),
	SYSCALL_ENTRY(fstatat64),
	SYSCALL_ENTRY(utimensat),
	SYSCALL_ENTRY(truncate64),
	SYSCALL_ENTRY(ftruncate64),
	SYSCALL_ENTRY(write),
	SYSCALL_ENTRY(writev),
	SYSCALL_ENTRY(pwrite64),
	SYSCALL_ENTRY(pwritev),
	SYSCALL_ENTRY(read),
	SYSCALL_ENTRY(readv),
	SYSCALL_ENTRY(pread64),
	SYSCALL_ENTRY(preadv),
	SYSCALL_ENTRY(mount),
	SYSCALL_ENTRY(ioctl),
	SYSCALL_ENTRY(_llseek),
	SYSCALL_ENTRY(dup),
	SYSCALL_ENTRY(dup2),
	SYSCALL_ENTRY(pipe2),
	SYSCALL_ENTRY(pipe),
	SYSCALL_ENTRY(execve),
	SYSCALL_ENTRY(mknod),
	SYSCALL_ENTRY(mknodat),
	SYSCALL_ENTRY(mkdir),
	SYSCALL_ENTRY(mkdirat),
	SYSCALL_ENTRY(symlink),
	SYSCALL_ENTRY(symlinkat),
	SYSCALL_ENTRY(unlinkat),
	SYSCALL_ENTRY(unlink),
	SYSCALL_ENTRY(rmdir),
	SYSCALL_ENTRY(rename),
	SYSCALL_ENTRY(renameat),
	SYSCALL_ENTRY(readlinkat),
	SYSCALL_ENTRY(readlink),
	SYSCALL_ENTRY(getdents64),
	SYSCALL_ENTRY(fcntl64),
	SYSCALL_ENTRY(getcwd),
	SYSCALL_ENTRY(chdir),
	SYSCALL_ENTRY(fchdir),
	SYSCALL_ENTRY(chroot),
	SYSCALL_ENTRY(faccessat),
	SYSCALL_ENTRY(access),
	SYSCALL_ENTRY(umask),
	SYSCALL_ENTRY(fadvise64_64),
	SYSCALL_ENTRY(poll),
	SYSCALL_ENTRY(ppoll),
	SYSCALL_ENTRY(pselect6),
	SYSCALL_ENTRY(_newselect),
	SYSCALL_ENTRY(statfs64),
	SYSCALL_ENTRY(fstatfs64),

	/*
	 * Memory management syscalls
	 */
	SYSCALL_ENTRY(mprotect),
	SYSCALL_ENTRY(mmap2),
	SYSCALL_ENTRY(munmap),
	SYSCALL_ENTRY(brk),

	/*
	 * Time syscalls
	 */
	SYSCALL_ENTRY(clock_gettime),
	SYSCALL_ENTRY(nanosleep),

	/*
	 * Network syscalls
	 */
	SYSCALL_ENTRY(socketcall),
};

static syscall_t *syscall_get(uint32_t num) {
	if(num > NELEM(syscalls)) {
		return NULL;
	}

	return &syscalls[num];
}

#define DBG_SCALL  	0
#define DBG_SCALL_RET	0
#define DBG_SCALL_FAIL	0

void syscall(void) {
	thread_t *thread = cur_thread();
	trapframe_t *frame;
	uint32_t num;
	syscall_t *sys;
	int retval;

	frame = thread->trapframe;
	assert(frame);

	num = tf_syscall_num(frame);
	thread->syscall = num;
	sys = syscall_get(num);
	if(!sys || !sys->func) {
		kprintf("[SYSCALL] Unknown: %u\n", num);
		/* kprintf("\t0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", r->ebx, r->ecx,
			r->edx,	r->esi, r->edi, r->ebp); */
		kpanic("TODO");
		retval = -ENOSYS;
	} else  {
#if DBG_SCALL
#if 0
		if(num != SYS_read && num != SYS_pread64 && num != SYS_preadv &&
			num != SYS_readv && num != SYS_write &&
			num != SYS_pwrite64 && num != SYS_pwritev &&
			num != SYS_writev)
#endif
		kprintf("[SYSCALL] %s 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x (%d)\n",
			sys->name, frame->ebx, frame->ecx, frame->edx,
			frame->esi, frame->edi, frame->ebp, cur_thread()->tid);
#endif

		retval = tf_do_syscall(frame, sys->func);

#if DBG_SCALL_RET
		kprintf("[SYSCALL] %s => %d (%d)\n", sys->name, retval,
			cur_thread()->tid);
#endif
#if DBG_SCALL_FAIL
		if(retval < 0) {
			if(num != SYS_brk && num != SYS_wait4)
			kprintf("[SYSCALL] %s failed (%d)\n", sys->name,
				retval);
		}
#endif
	}

	if(retval == -ERESTART) {
		assert(num == SYS_wait4);
	}

	if(critsect_p()) {
		kpanic("syscall: %s did not leave critical section\n",
			sys->name);
	}

	/*
	 * Should not modify any registers after sys_sigreturn and sys_execve
	 * => do not call tf_set_retval()
	 */
	if(num != SYS_sigreturn && num != SYS_execve) {
		tf_set_retval(frame, retval);
	}
}

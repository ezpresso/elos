#ifndef SYS_SIGNAL_H
#define SYS_SIGNAL_H

#include <sys/arch/signal.h>

#define SIG_ERR		((void (*)(int))-1)
#define SIG_DFL		((void (*)(int)) 0)
#define SIG_IGN		((void (*)(int)) 1)

#define SIG_BLOCK	0
#define SIG_UNBLOCK	1
#define SIG_SETMASK	2

#define SA_NOCLDSTOP 	1
#define SA_NOCLDWAIT 	2
#define SA_SIGINFO	4

#define SA_ONSTACK	0x08000000
#define SA_RESTART	0x10000000
#define SA_NODEFER	0x40000000
#define SA_RESETHAND	0x80000000
#define SA_RESTORER	0x04000000

#define SIGHUP		1
#define SIGINT		2
#define SIGQUIT		3
#define SIGILL		4
#define SIGTRAP		5
#define SIGABRT		6
#define SIGIOT		SIGABRT
#define SIGBUS		7
#define SIGFPE		8
#define SIGKILL		9
#define SIGUSR1		10
#define SIGSEGV		11
#define SIGUSR2		12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGSTKFLT 	16
#define SIGCHLD		17
#define SIGCONT		18
#define SIGSTOP		19
#define SIGTSTP		20
#define SIGTTIN		21
#define SIGTTOU		22
#define SIGURG		23
#define SIGXCPU		24
#define SIGXFSZ		25
#define SIGVTALRM 	26
#define SIGPROF		27
#define SIGWINCH 	28
#define SIGIO		29
#define SIGPOLL		29
#define SIGPWR		30
#define SIGSYS		31
#define SIGUNUSED	SIGSYS

#define NSIG 32

#define SS_ONSTACK    1
#define SS_DISABLE    2
#define SS_AUTODISARM (1U << 31)

typedef struct sigaltstack {
	void *ss_sp;
	int ss_flags;
	size_t ss_size;
} stack_t;

#define SIGSET_IDX(sig)	(((sig) >> 3) / sizeof(unsigned long))
#define SIGSET_MSK(sig)	(1UL << ((sig) & ((sizeof(unsigned long) << 3) - 1)))
#define SIGSET_NELEM 	MAX(1, _NSIG / (sizeof(unsigned long) << 3))
typedef struct sigset {
	unsigned long sig[SIGSET_NELEM];
} sigset_t;

/* TODO not true for every architecture */
typedef struct sigaction {
	void (*sa_handler) (int);
	unsigned long sa_flags;
	void (*sa_restorer) (void);
	sigset_t sa_mask;
} sigaction_t;

typedef struct ucontext {
	unsigned long uc_flags;
	struct ucontext *uc_link;
	stack_t uc_stack;
	sigcontext_t uc_mcontext;
	sigset_t uc_sigmask;
} ucontext_t;

#endif

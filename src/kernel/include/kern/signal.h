#ifndef KERN_SIGNAL_H
#define KERN_SIGNAL_H

#include <sys/signal.h>

struct trapframe;
struct proc;
struct thread;

/* Send signal without checking whether the current
 * process has the rights to send it.
 */
#define SIG_KERN (1 << 0)

int kern_tkill(struct thread *thread, int sig, int flags);
int kern_tkill_cur(int sig);
int kern_kill(struct proc *proc, int sig, int flags);
int kern_sigprocmask(int how, sigset_t *set, sigset_t *oldset);

bool signal_is_masked(int sig);
bool signal_is_ignored(int sig);

void signal_set_mask(sigset_t *set);
void signal_intr(void);

/* defined by arch */
int arch_sig_handle(struct thread *thr, int sig, stack_t *stack,
	sigaction_t *sigact, sigset_t *mask);
int arch_sigreturn(struct thread *thr);

static inline void sigset_or(sigset_t *set, sigset_t *block) {
	for(size_t i = 0; i < SIGSET_NELEM; i++) {
		set->sig[i] |= block->sig[i];
	}
}

static inline void sigset_nand(sigset_t *set, sigset_t *block) {
	for(size_t i = 0; i < SIGSET_NELEM; i++) {
		set->sig[i] &= ~block->sig[i];
	}
}

static inline bool sigblocked(sigset_t *set, int sig) {
	sig -= 1;
	return set->sig[SIGSET_IDX(sig)] & SIGSET_MSK(sig);
}

static inline void sigblock(sigset_t *set, int sig) {
	sig -= 1;
	set->sig[SIGSET_IDX(sig)] |= SIGSET_MSK(sig);
}

static inline void sigunblock(sigset_t *set, int sig) {
	sig -= 1;
	set->sig[SIGSET_IDX(sig)] &= ~SIGSET_MSK(sig);
}

static inline bool sigpending(sigset_t *set, sigset_t *mask) {
	for(size_t i = 0; i < SIGSET_NELEM; i++) {
		if(set->sig[i] & ~mask->sig[i]) {
			return true;
		}
	}

	return false;
}

int sys_sigreturn(void);
int sys_sigaltstack(stack_t *uss, stack_t *oss);
int sys_rt_sigprocmask(int how, sigset_t *set, sigset_t *oldset, size_t size);
int sys_rt_sigaction(int signum, sigaction_t *act, sigaction_t *oldact,
	size_t size);
int sys_kill(pid_t pid, int sig);
int sys_tkill(int tid, int sig);

#endif

#ifndef KERN_FUTEX_H
#define KERN_FUTEX_H

struct timespec;

#define KWAIT_USR	(1 << 0)
#define KWAIT_INTR	(1 << 1)
#define KWAIT_PRIV	(1 << 2)

#define kern_wait_timeout(addr, val, flags, timeout) ({			\
	typeof(val) __wait_tmp = (val);					\
	typeof(addr) __wait_vptr = &__wait_tmp;				\
	__kern_wait(addr, sizeof(*(addr)), __wait_vptr, flags, timeout); \
})

#define kern_wait(addr, val, flags) ({			\
	kern_wait_timeout(addr, val, flags, NULL);	\
})

/*
typedef struct robust_list {
	struct robust_list *next;
} robust_list_t;

typedef struct robust_list_head {
	robust_list_t list;
	long futex_offset;
	struct robust_list *list_op_pending;
} robust_list_head_t;
*/

int __kern_wait(void *addr, size_t val_size, void *val, int flags,
	struct timespec *timeout);

int kern_wake(void *addr, int num, int flags);

int sys_futex(int *uaddr, int op, int val, const struct timespec *timeout,
	int *uaddr2, int val3);

void init_futex(void);

#endif
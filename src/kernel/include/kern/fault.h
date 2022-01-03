#ifndef KERN_FAULT_H
#define KERN_FAULT_H

#include <kern/proc.h>
#include <kern/setjmp.h>

static inline void __mayfault_cleanup(__unused jmp_buf_t *i) {
	cur_thread()->onfault = NULL;
}

#define mayfault(label)					\
	for(jmp_buf_t _mfbuf				\
		__cleanup(__mayfault_cleanup),		\
		*_mfi = ({				\
			thread_t *thr = cur_thread();	\
			thr->onfault = &_mfbuf;		\
			if(setjmp(&_mfbuf)) {		\
				goto label;		\
			}				\
			NULL; 				\
		});					\
		_mfi == NULL; _mfi = (void *)1)

#endif
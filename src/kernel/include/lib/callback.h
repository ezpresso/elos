#ifndef LIB_CALLBACK_H
#define LIB_CALLBACK_H

#include <sys/errno.h>

#define CB(err) (void *)__cb_ ## err

static inline void __cb_NOOP(void) { }
static inline int __cb_SUCCESS(void) { return 0; }
static inline int __cb_ZERO(void) { return 0; }
static inline void *__cb_NULL(void) { return NULL; }

#define __DEFINE_ERR_CB(ERR)		\
static inline int __cb_##ERR(void) {	\
	return -ERR;			\
}

/**
 * TODO add more
 */
__DEFINE_ERR_CB(EPERM)
__DEFINE_ERR_CB(ENOENT)
__DEFINE_ERR_CB(ESRCH)
__DEFINE_ERR_CB(EINVAL)
__DEFINE_ERR_CB(EROFS)

#endif
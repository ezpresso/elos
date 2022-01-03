#ifndef KERN_RWLOCK_H
#define KERN_RWLOCK_H

#include <kern/spinlock.h>

#define RWLOCK_INIT (rwlock_t) {	\
	.lock = SPINLOCK_INIT,		\
	.rdwait = 0,			\
	.rfutex = 0,			\
	.wfutex = 0,			\
}

/*
 * Arguments for rwlock_assert.
 */
#define RWLOCK_RD 0
#define RWLOCK_WR 1

typedef struct rwlock {
	spinlock_t lock;
	uint16_t rdwait;

	union {
		struct {
			uint16_t wrwait;
			uint8_t wrlock;
		};
		uint32_t rfutex;
	};

	union {
		struct {
			uint16_t rdnum;
			uint8_t wrlock2;
		};
		uint32_t wfutex;
	};
} rwlock_t;

void rwlock_init(rwlock_t *lock);
static inline void rwlock_destroy(rwlock_t *lock) {
	(void) lock;
}

void wrlock(rwlock_t *lock);
void rdlock(rwlock_t *lock);
void rwunlock(rwlock_t *lock);
void rwlock_cleanup(rwlock_t **lock);

#define rwlock_assert(l, t) ({						\
	if((t) == RWLOCK_RD) {						\
		kassert(((l)->rdnum > 0 || (l)->wrlock),		\
			"[rwlock] rwlock has to be read-locked");	\
	} else if((t) == RWLOCK_WR) {					\
		kassert((l)->wrlock, "[rwlock] rwlock has to be write locked"); \
	} else {							\
		kpanic("[rwlock] rwlock_assert: invalid argument: %d", (t)); \
	}								\
})

#define __rwlock_scope_lock_cond(l, lock, b) 		\
	__cleanup_var(rwlock_cleanup, rwlock_t *) =	\
	(b) ? ({ lock(l); (l); }) : NULL

#define __rwlocked_cond_internal(l, lock, b, dummy) 			\
	for(__rwlock_scope_lock_cond(l, lock, b), * dummy = NULL;	\
		dummy == NULL; dummy = (void *)1)

#define __rwlocked_cond(l, lock, b) \
	__rwlocked_cond_internal(l, lock, b, UNIQUE_NAME(__rwl_dummy))

#define rdlocked_cond(l, b) 	__rwlocked_cond(l, rdlock, b)
#define wrlocked_cond(l, b) 	__rwlocked_cond(l, wrlock, b)
#define rdlocked(l) 		rdlocked_cond(l, true)
#define wrlocked(l)		wrlocked_cond(l, true)
#define rdlock_scope_cond(l, b) __rwlock_scope_lock_cond(l, rdlock, b)
#define wrlock_scope_cond(l, b) __rwlock_scope_lock_cond(l, wrlock, b)
#define rdlock_scope(l) 	rdlock_scope_cond(l, true)
#define wrlock_scope(l) 	wrlock_scope_cond(l, true) 

#endif
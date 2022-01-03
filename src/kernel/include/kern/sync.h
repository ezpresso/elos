#ifndef KERN_SYNC_H
#define KERN_SYNC_H

#include <lib/development.h>

/* Example:
 *
 * sync_t lock;
 * sync_init(&lock, SYNC_SPINLOCK);
 *
 * synchronized(&lock) {
 *	kprintf("Hello locked world\n");
 * }
 * kprintf("Hello unlocked world\n");
 */

#define SYNC_SPINLOCK	0
#define SYNC_MUTEX	1

#define __SYNC_INIT(t)				\
	{					\
		.waiting = 0, 			\
		.type = SYNC_ ## t,		\
		.thread = NULL, 		\
		MAGIC_INIT(magic, SYNC_MAGIC),	\
		DEVEL_INIT(line, -1),		\
		DEVEL_INIT(file, NULL),		\
	}

#define SYNC_INIT(t) (sync_t) __SYNC_INIT(t)

#define DEFINE_SYNC(name, type) \
	sync_t name = __SYNC_INIT(type)

#define sync_scope_acquire_cond(sync, b)			\
	__cleanup_var(__sync_cleanup, sync_t *) = (b) ? ({	\
		sync_t *_sync = (sync);				\
		sync_acquire(_sync); (_sync); 			\
	}) : NULL

#define __synchronized_cond(sync, name, dummy, b)		\
	for(sync_scope_acquire_cond(sync, b), * dummy = NULL;	\
		dummy == NULL; dummy = (void *)1)

#define synchronized_cond(sync, b)				\
	__synchronized_cond(sync, UNIQUE_NAME(__sync_cleanup),	\
		UNIQUE_NAME(__sync_dummy), b)

#define sync_scope_acquire(sync) \
	sync_scope_acquire_cond(sync, true)

#define synchronized(sync) \
	synchronized_cond(sync, true)

typedef struct sync {
	struct thread *thread;
	uint16_t waiting;
	uint8_t type;

	DEVEL_VAR(short, line);
	DEVEL_VAR(const char *, file);
	MAGIC(magic);
} sync_t;

void sync_init(sync_t *sync, int type);

static inline void sync_destroy(sync_t *sync) {
	magic_destroy(&sync->magic);
}

#define sync_trylock(s) __sync_trylock(s, __FILE__, __LINE__)
bool __sync_trylock(sync_t *sync, const char *file, int line);

#define sync_acquire(s) __sync_acquire(s, __FILE__, __LINE__)
void __sync_acquire(sync_t *sync, const char *file, int line);
void sync_release(sync_t *sync);

#define sync_assert(s) kassert(__sync_assert(s), "[sync] lock not locked")
bool __sync_assert(sync_t *sync);

static inline void __sync_cleanup(sync_t **sync_ptr) {
	sync_t *sync = *sync_ptr;

	if(sync != NULL) {
		sync_release(sync);
	}
}

#endif

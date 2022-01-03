#ifndef KERN_SPINLOCK_H
#define KERN_SPINLOCK_H

#define SPIN_LOCKED 	1
#define SPIN_UNLOCKED	0
typedef uint8_t spinlock_t;

#define SPINLOCK_INIT SPIN_UNLOCKED

static inline void spinlock_init(spinlock_t *lock) {
	*lock = SPINLOCK_INIT;
}

bool spin_try_lock(spinlock_t *lock);
bool spin_locked(spinlock_t *lock);
void spin_lock(spinlock_t *lock);

#define spin_unlock(l)	__spin_unlock(l, __FILE__, __LINE__)
void __spin_unlock(spinlock_t *lock, char *file, int line);

#endif
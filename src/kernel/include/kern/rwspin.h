#ifndef KERN_RWSPIN_H
#define KERN_RWSPIN_H

#define RWLOCK_SPIN_INIT (rwlock_spin_t){ .ticket = 0, .rw = 0 }

typedef struct rwlock_spin {
	union {
		struct {
			uint16_t write;
			uint16_t read;
		};
		uint32_t rw;
	};

	uint16_t ticket; /* next ticket */
} rwlock_spin_t;

void rwlock_spin_init(rwlock_spin_t *l);
static inline void rwlock_spin_destroy(rwlock_spin_t *lock) {
	(void) lock;
}

void rdlock_spin(rwlock_spin_t *l);
void rdunlock_spin(rwlock_spin_t *l);
void wrlock_spin(rwlock_spin_t *l);
void wrunlock_spin(rwlock_spin_t *l);

#endif
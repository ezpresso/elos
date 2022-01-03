#ifndef KERN_ATOMIC_H
#define KERN_ATOMIC_H

#define __atomic _Atomic

#define atomic_thread_fence(type) \
	__atomic_thread_fence(__ ## type)
#define atomic_store_order(ptr, val, type) \
	__atomic_store_n(ptr, val, __ ## type)
#define atomic_load_order(ptr, type) \
	__atomic_load_n(ptr, __ ## type)

#define atomic_xchg(ptr, val)	__atomic_exchange_n(ptr, val, __ATOMIC_SEQ_CST)
#define atomic_add(ptr, val)	__atomic_fetch_add(ptr, val, __ATOMIC_SEQ_CST)
#define atomic_sub(ptr, val)	__atomic_fetch_sub(ptr, val, __ATOMIC_SEQ_CST)
#define atomic_inc(ptr)		atomic_add(ptr, 1)
#define atomic_dec(ptr)		atomic_sub(ptr, 1)
#define atomic_and(ptr, val)	__atomic_fetch_and(ptr, val, __ATOMIC_SEQ_CST)
#define atomic_or(ptr, val)	__atomic_fetch_or(ptr, val, __ATOMIC_SEQ_CST)
#define atomic_store(ptr, val)	__atomic_store_n(ptr, val, __ATOMIC_SEQ_CST)
#define atomic_load(ptr)	__atomic_load_n(ptr, __ATOMIC_SEQ_CST)
#define atomic_cmpxchg(ptr, old, new)  ({ 				\
	typeof(*ptr) __atomic_tmp = old;  				\
	__atomic_compare_exchange_n(ptr, &__atomic_tmp, new, false,	 \
		__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);  			\
})

#define atomic_cmpxchg_val(ptr, old, new)  ({  				\
	typeof(*ptr) __atomic_tmp = old;  				\
	__atomic_compare_exchange_n(ptr, &__atomic_tmp, new, false,	\
		__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);  			\
	__atomic_tmp;  							\
})

#define atomic_xchg_relaxed(ptr, val) \
	__atomic_exchange_n(ptr, val, __ATOMIC_RELAXED)
#define atomic_add_relaxed(ptr, val) \
	__atomic_fetch_add(ptr, val, __ATOMIC_RELAXED)
#define atomic_sub_relaxed(ptr, val) \
	__atomic_fetch_sub(ptr, val, __ATOMIC_RELAXED)
#define atomic_inc_relaxed(ptr) \
	atomic_add_relaxed(ptr, 1)
#define atomic_dec_relaxed(ptr) \
	atomic_sub_relaxed(ptr, 1)
#define atomic_and_relaxed(ptr, val) \
	__atomic_fetch_and(ptr, val, __ATOMIC_RELAXED)
#define atomic_or_relaxed(ptr, val) \
	__atomic_fetch_or(ptr, val, __ATOMIC_RELAXED)
#define atomic_store_relaxed(ptr, val) \
	__atomic_store_n(ptr, val, __ATOMIC_RELAXED)
#define atomic_load_relaxed(ptr) \
	__atomic_load_n(ptr, __ATOMIC_RELAXED)
#define atomic_cmpxchg_relaxed(ptr, old, new)  ({  			\
	typeof(old) __atomic_tmp = old;  				\
	__atomic_compare_exchange_n(ptr, &__atomic_tmp, new, false,	\
		__ATOMIC_RELAXED, __ATOMIC_RELAXED);  			\
})
#define atomic_cmpxchg_relaxed_val(ptr, old, new)  ({  			\
	typeof(old) __atomic_tmp = old;  				\
	__atomic_compare_exchange_n(ptr, &__atomic_tmp, new, false,	\
		__ATOMIC_RELAXED, __ATOMIC_RELAXED);  			\
	__atomic_tmp;  							\
})
#define atomic_xchg_acquire(ptr, val) \
	__atomic_exchange_n(ptr, val, __ATOMIC_ACQUIRE)
#define atomic_load_acquire(ptr) \
	__atomic_load_n(ptr, __ATOMIC_ACQUIRE)
#define atomic_store_release(ptr, val) \
	__atomic_store_n(ptr, val, __ATOMIC_RELEASE)

static inline void atomic_loadn(void *buf, const void *atom_buf, size_t size) {
	switch(size) {
	case 1:
		*(uint8_t *)buf = atomic_load((uint8_t *)atom_buf);
		return;
	case 2:
		*(uint16_t *)buf = atomic_load((uint16_t *)atom_buf);
		return;
	case 4:
		*(uint32_t *)buf = atomic_load((uint32_t *)atom_buf);
		return;
	case 8:
		/* *(uint64_t *)buf = atomic_load((uint64_t *)atom_buf);
		return; */
		/* See atomic_storen for reason */
	/* FALLTHROUGH */
	default:
		kpanic("[atomic] atomic_loadn: unsupported size: %d", size);
	}
}

static inline void atomic_storen(void *atom_buf, const void *buf, size_t size) {
	switch(size) {
	case 1:
		atomic_store((uint8_t *)atom_buf, *(uint8_t *)buf);
		return;
	case 2:
		atomic_store((uint16_t *)atom_buf, *(uint16_t *)buf);
		return;
	case 4:
		atomic_store((uint32_t *)atom_buf, *(uint32_t *)buf);
		return;
	case 8:
		/* Yay, my GCC 7.2.0 is completely retarded. Version 6.3.0
		 * could still use cmpxchg8b even in 32bit mode and version
		 * 7.2.0 cannot do that for some weird reason...
		 */
		/* atomic_store((uint64_t *)atom_buf, *(uint64_t *)buf);
		return; */
	/* FALLTHROUGH */
	default:
		kpanic("[atomic] atomic_storen: unsupported size: %d", size);
	}
}

typedef uint32_t ref_t;

static inline void ref_init_val(ref_t *ref, uint32_t val) {
	atomic_store_relaxed(ref, val);
}

static inline void ref_init(ref_t *ref) {
	ref_init_val(ref, 1);
}

static inline uint32_t ref_inc(ref_t *ref) {
	uint32_t retv = atomic_inc_relaxed(ref);
	kassert(retv < UINT32_MAX, "[atomic] refcnt: overflow");
	return retv;
}

/* Returns true if there is no reference left */
static inline bool ref_dec(ref_t *ref) {
	uint32_t retv = atomic_dec_relaxed(ref);
	kassert(retv > 0, "[atomic] refcnt: underflow");
	return retv == 1;
}

static inline int ref_get(ref_t *ref) {
	return atomic_load_relaxed(ref);
}

#endif

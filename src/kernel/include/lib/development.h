#ifndef LIB_DEVELOPMENT_H
#define LIB_DEVELOPMENT_H

#include <config.h>
#include <compiler/asan.h>

#define POISON_FREE	0xeb /* Memory which is freed is filled with this */
#define POSION_ALLOC	0xac /* Memory allocated without the VM_ZERO flag */
#define LIST_MAGIC	0xaabcdeff
#define SYNC_MAGIC 	0xdeadcafe
#define NOMAGIC 	0xffccddee

#if CONFIGURED(DEVELOPMENT)

/**
 * @brief Declare a value used for some checking in a development build
 */
#define DEVEL_VAR(type, name) type name

/**
 * @brief Set a development variable.
 */
#define DEVEL_SET(var, val) (var) = (val)

/**
 * @brief Initialize a dev value when statically initialized in a structure.
 */
#define DEVEL_INIT(name, val) .name = val

/**
 * @brief Declare a variable storing a magic number.
 */
#define MAGIC(name) DEVEL_VAR(unsigned, name)

/**
 * @brief Initialize a magic value when statically initialized in a structure.
 */
#define MAGIC_INIT(name, magic) DEVEL_INIT(name, magic)

/**
 * @brief Protect a development variable from being written to.
 */
#define devel_prot(var) asan_prot(var, sizeof(*(var)))

/**
 * @brief Remove the protection of a development variable.
 */
#define devel_rmprot(var) asan_rmprot(var, sizeof(*(var)))

#define devel_assert(ptr, val, msg...) \
	kassert(*(ptr) == (typeof(*(ptr)))(val), msg)

/**
 * @brief Initialize a magic value.
 */
#define magic_init(ptr, magic) ({	\
	*(ptr) = magic;			\
	devel_prot(ptr);		\
})

/**
 * @brief Destroy a magic value.
 */
#define magic_destroy(ptr) ({	\
	devel_rmprot(ptr);	\
	*(ptr) = NOMAGIC;	\
})

/**
 * @brief Sanity check a magic variable.
 */
#define magic_check(ptr, value) ({ \
	kassert(*(ptr) == value, "[dev] invalid magic value: 0x%x != 0x%x", \
		*(ptr), value);	\
})

#else

#define DEVEL_VAL(type, name)
#define DEVEL_SET(name, val)
#define DEVEL_INIT(name, val)
#define MAGIC(name)
#define MAGIC_INIT(name, magic)
#define devel_prot(var)
#define devel_rmprot(var)
#define devel_assert(var, val, msg...)
#define magic_init(ptr, magic) (void) ptr
#define magic_destroy(ptr) (void) ptr
#define magic_check(ptr) (void) ptr

#endif
#endif

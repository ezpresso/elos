#ifndef KERN_STACK_H
#define KERN_STACK_H

#include <kern/user.h>
#include <lib/string.h>

/* TODO upwards growing stack */
typedef struct kstack {
	void *start;
	void *ptr;
	size_t size;
} kstack_t;

/**
 * @brief Initialize a structure describing a stack.
 *
 * @param start The beginning of the stack memory.
 * @param size  The size of the stack.
 */
static inline void stack_init(kstack_t *stack, void *start, size_t size) {
	if(size == 0) {
		stack->ptr = start;
		stack->start = NULL;
	} else {
		stack->start = start;
		stack->ptr = start + size;
	}

	stack->size = size;
}

/**
 * @brief Get the current stack pointer of a kstack structure.
 */
static inline void *stack_pointer(kstack_t *stack) {
	return stack->ptr;
}

/**
 * @brief Get the current stack pointer address of a kstack structure.
 */
static inline uintptr_t stack_addr(kstack_t *stack) {
	return (uintptr_t) stack->ptr;
}

/**
 * @brief Allocate some memory from a kstack.
 *
 * Decreases (TODO or increases) the stack pointer of a kstack structure.
 * The resulting pointer will be aligned.
 *
 * @param stack The kstack structure.
 * @param size  The size of the memory being allocated.
 * @param align The alignment of the memory being allocated.
 */
static inline void *stack_rsv_align(kstack_t *stack, size_t size,
	size_t align)
{
	if(stack->size && (size_t)(stack->ptr - stack->start) < size) {
		return NULL;
	}

	stack->ptr -= size;
	stack->ptr = ALIGN_PTR_DOWN(stack->ptr, align);
	return stack->ptr;
}

/**
 * @brief Allocate some memory from a kstack without any special alignment.
 * @see stack_rsv_align
 */
static inline void *stack_rsv(kstack_t *stack, size_t size) {
	return stack_rsv_align(stack, size, 1);
}

/**
 * @brief Allocate memory from a kstack for a specific type.
  * @see stack_rsv_align
 */
#define stack_rsv_type(stack, type) ({					\
	stack_rsv_align(stack, sizeof(type), __alignof__(type));	\
})

/**
 * @brief Push a value onto the stack.
 */
#define stack_pushval(stack, val) ({				\
	typeof(val) *__tmp = stack_rsv_type(stack, val);	\
	if(__tmp) *__tmp = (val);				\
	__tmp;							\
})

/**
 * @brief Pop a value from the stack.
 */
static inline void *stack_pop(kstack_t *stack, size_t size) {
	void *ptr = stack->ptr;
	stack->ptr += size;
	return ptr;
}

/**
 * @brief Copy memory onto a stack in userspace.
 */
static inline int stack_copyout(kstack_t *stack, const void *ptr, size_t size,
	void **pptr)
{
	void *dst = stack_rsv(stack, size);
	if(dst == NULL) {
		return -ENOSPC;
	} else {
		*pptr = dst;
		return copyout(dst, ptr, size);
	}
}

/**
 * @brief Copy a value to a stack in userspace.
 */
#define stack_copyout_val(stack, value) ({ \
	typeof(value) *__tmp = stack_rsv_type(stack, value), __val = value; \
	int __ret;						\
	if(__tmp == NULL) {					\
		__ret = -ENOSPC;				\
	} else {						\
		__ret = copyout(__tmp, &__val, sizeof(value));	\
	}																	\
	__ret;																\
})

#endif
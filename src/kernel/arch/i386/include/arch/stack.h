#ifndef ARCH_STACK_H
#define ARCH_STACK_H

#include <config.h>

#if CONFIGURED(STACKCHK)
typedef struct stack_canary {
	/* canary is %gs:0x14 */
	uint8_t __pad[20];
	uintptr_t canary;
} stack_canary_t;

static inline uintptr_t stack_canary_init(stack_canary_t *canary) {
	extern uintptr_t __stack_chk_guard;
	canary->canary = __stack_chk_guard;
	return (uintptr_t)canary;
}

#else

#define stack_canary_init(x) 0
typedef struct stack_canary {
} stack_canary_t;

#endif /* CONFIG(STACKCHK) */
#endif /* ARCH_STACK_H */

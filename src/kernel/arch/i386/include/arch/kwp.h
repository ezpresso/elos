#ifndef ARCH_KWP_H
#define ARCH_KWP_H

#include <arch/x86.h>
#include <kern/critical.h>

/*
 * Kernel-write-protect (kwp)
 *
 * Enables write protect in kernel mode
 * (fault on write access on read only pages)
 */

static inline void kwp_enable(void) {
	cr0_set(cr0_get() | CR0_WP);
}

static inline void kwp_disable(void) {
	critical_enter();
	cr0_set(cr0_get() & ~CR0_WP);
}

static inline void kwp_reenable(void) {
	kwp_enable();
	critical_leave();
}

static inline bool kwp_enabled(void) {
	return !!(cr0_get() & CR0_WP);
}

#endif
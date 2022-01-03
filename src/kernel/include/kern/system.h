#ifndef KERN_SYSTEM_H
#define KERN_SYSTEM_H

#include <kern/types.h>
#include <arch/system.h>
#include <sys/stdarg.h>
#include <sys/errno.h>
#include <elos/elos.h>
#include <elos/bitops.h>

#define KERN_MINOR 1
#define KERN_MAJOR 0
#define KERN_VERSION STR(KERN_MAJOR) "." STR(KERN_MINOR)

/**
 * @brief The page size in bytes.
 */
#define PAGE_SZ	(1U << PAGE_SHIFT)

/**
 * @brief Mask the page bits of an address.
 */
#define PAGE_MASK (~(PAGE_SZ-1))

/**
 * @brief Convert an address into a page index.
 */
#define atop(a) ((a) >> PAGE_SHIFT)

/**
 * @brief Convert a page index into an.
 */
#define ptoa(p) ((p) << PAGE_SHIFT)

/**
 * @brief Check if a memory range is inside one page.
 */
#define INSIDE_PAGE(ptr, size) \
	(atop((uintptr_t)(ptr) + (size)) == atop((uintptr_t)(ptr)))

#define __FIRST(x, y...) x

/**
 * @brief Kernel assertion.
 */
#define assert(a) kassert(a, NULL)
#define kassert(a, msg...) 							\
	if(unlikely(!(a))) {							\
		if(__FIRST(msg, 0) == NULL) {					\
			__kassert_fail(__FILE__, __LINE__, "\"" STR(a) "\"");	\
		} else {							\
			__kassert_fail(__FILE__, __LINE__, msg);		\
		}								\
	}

/**
 * @brief Cause a kernel panic.
 *
 * If something unexpected, which cannot be handled, the kernel
 * enters kernel panic mode and the system is halted.
 */
#define kpanic(b...) __kpanic(__FILE__, __LINE__, b)
void __noreturn __kpanic(const char *f, int l, const char *fmt, ...)
	__printf_format(3, 4);
void __noreturn __kassert_fail(const char *f, int l, const char *fmt, ...)
	__printf_format(3, 4);
int kprintf(const char *format, ...) __printf_format(1, 2);
int va_kprintf(const char *fmt, va_list arp);

#endif

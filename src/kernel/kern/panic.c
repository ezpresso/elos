/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 * 
 * Copyright (c) 2018, Elias Zell
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <kern/system.h>
#include <kern/symbol.h>
#include <kern/stacktrace.h>
#include <kern/critical.h>
#include <kern/cpu.h>
#include <kern/log.h>
#include <kern/proc.h>
#include <kern/mp.h>
#include <lib/string.h>

/**
 * @brief The size of the kernel panic buffer
 */
#define KPANIC_BUFFER 2048

static char kpanic_buf[KPANIC_BUFFER];
static size_t kpanic_size = KPANIC_BUFFER;
static char *kpanic_ptr = kpanic_buf;
static thread_t *kpanic_thread;

/**
 * @brief Write to the kernel panic buffer.
 */
static inline void kpanic_vsnprintf(const char *fmt, va_list args) {
	size_t size;

	/*
	 * The kernel panic buffer is full. New information is just
	 * discarded and as much information as possible is
	 * displayed later on.
	 */
	if(kpanic_size == 0) {
		return;
	}

	size = vsnprintf(kpanic_ptr, kpanic_size, fmt, args);
	if(size > kpanic_size) {
		kpanic_size = 0;
	} else {
		kpanic_size -= size;
		kpanic_ptr += size;
	}
}

/**
 * @brief Write to the kernel panic buffer.
 */
static inline void kpanic_snprintf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	kpanic_vsnprintf(fmt, ap);
	va_end(ap);
}

/**
 * @brief Enter kernel panic mode.
 */
static void kpanic_enter(void) {
	thread_t *thread = cur_thread();

	critical_enter();
	if(atomic_cmpxchg(&kpanic_thread, NULL, thread) == false) {
		/*
		 * It is possible that the thread currently handling the
		 * panic causes another panic. Remember, the kernel is
		 * in an invalid state in this case.
		 */
		if(kpanic_thread == thread) {
			 /* 
			  * Leaving the critical section is technically not
			  * needed.
			  */
			critical_leave();
			return;
		}

		for(;;) ;
	}

	ipi_panic();
}

/**
 * @brief Append a stacktrace to the panic buffer and print the complete buffer.
 */
static __noreturn void kpanic_done(void) {
	proc_t *proc = cur_proc();
	uintptr_t ip;

	/*
	 * Print some informatoion about the one who called kpanic.
	 */
	kpanic_snprintf("\nthread: %d\n", cur_thread()->tid);
	kpanic_snprintf("process: %d, %s\n", cur_proc()->pid,
		proc->image->binary);

	/*
	 * Print the stack trace.
	 */
	kpanic_snprintf("kernel stacktrace:\n");
	stacktrace_foreach(&ip) {
		kpanic_snprintf("\t0x%x\n", ip);
	}
	kpanic_snprintf("halting...\n");	

	/*
	 * Print the complete buffer.
	 */
	log_panic(kpanic_buf);

	/*
	 * Sleep for ever.
	 */
	for(;;) ;
}

void __noreturn __kpanic(const char *file, int line, const char *fmt, ...) {
	va_list ap;

	kpanic_enter();
	kpanic_snprintf("[panic] kernel panic at %s:%d on CPU%d:\n\t", file,
		line, cur_cpu()->id);

	va_start(ap, fmt);
	kpanic_vsnprintf(fmt, ap);
	va_end(ap);
	kpanic_done();
}
export(__kpanic);

void __noreturn __kassert_fail(const char *file, int line, const char *fmt,
	...)
{
	va_list ap;

	kpanic_enter();
	kpanic_snprintf("[panic] assertion failed at %s:%d on CPU%d:\n", file,
		line, cur_cpu()->id);

	va_start(ap, fmt);
	kpanic_vsnprintf(fmt, ap);
	va_end(ap);
	kpanic_done();
}

export(__kassert_fail);

bool kpanic_p(void) {
	return kpanic_thread != NULL;
}
export(kpanic_p);

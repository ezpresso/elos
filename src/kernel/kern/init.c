/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 *
 * Copyright (c) 2017, Elias Zell
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
#include <kern/init.h>
#include <vm/vmem.h>

#define INIT_DEBUG 0

extern initcall_t __initcall0_start[];
extern initcall_t __initcall1_start[];
extern initcall_t __initcall2_start[];
extern initcall_t __initcall3_start[];
extern initcall_t __initcall_end[];
extern uint32_t init_start_addr;
extern uint32_t init_end_addr;

initcall_t *init_calls[] = {
	__initcall0_start,
	__initcall1_start,
	__initcall2_start,
	__initcall3_start,
	__initcall_end
};

static size_t n_initcalls = NELEM(init_calls);
static int init_cur_level = -1;

int init_get_level(void) {
	return init_cur_level;
}

void init_level(uint8_t level) {
	initcall_t *fn;

	if(level >= n_initcalls) {
		return;
	}

	init_cur_level = level;
	for(fn = init_calls[level]; fn < init_calls[level + 1]; fn++) {
#if INIT_DEBUG
		kprintf("INIT: %s (%d)\n", fn->name, level);
#endif
		if(fn->func() == INIT_PANIC) {
			kpanic("[init] %s (%d) failed", fn->name, level);
		}
	}
}

void init_free(void) {
	init_cur_level = INIT_FINISHED;
	uintptr_t start = (uintptr_t) & init_start_addr;
	uintptr_t end = (uintptr_t) & init_end_addr;

	kprintf("[init] freeing init memory: 0x%x - 0x%x\n", start, end);
	vmem_free_backed((void *)start, end - start);
}

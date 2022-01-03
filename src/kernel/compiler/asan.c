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
#include <kern/symbol.h>
#include <kern/atomic.h>
#include <kern/panic.h>
#include <kern/init.h>
#include <compiler/asan.h>
#include <vm/vmem.h>
#include <vm/layout.h>
#include <vm/mmu.h>

#define ASAN_ALIGN_LOG	2
#define ASAN_ALIGN (1 << ASAN_ALIGN_LOG)
#define ASAN_IDX(x) ((x) >> (ASAN_ALIGN_LOG + 3))
#define ASAN_BIT(x) (1 << (((x) >> (ASAN_ALIGN_LOG)) & 7))

/**
 * @brief This buffer contains 1 bit per 4bytes of kernel space.
 */
static uint8_t asan_buffer[ASAN_IDX(ASAN_END - ASAN_START)];
static vm_vaddr_t asan_start, asan_end;
static bool asan = false;

void _asan_rmprot(vm_vaddr_t addr, vm_vsize_t size) {
	assert(ALIGNED(addr, ASAN_ALIGN));
	assert(ALIGNED(size, ASAN_ALIGN));

	addr -= ASAN_START;

	/*
	 * Mark the range as being protected in the bitset.
	 */
	for(size_t i = 0; i < size; i += ASAN_ALIGN) {
		atomic_and_relaxed(&asan_buffer[ASAN_IDX(addr + i)],
			~ASAN_BIT(addr + i));
	}
}

void _asan_prot(vm_vaddr_t addr, vm_vsize_t size) {
	vm_vaddr_t tmp;

	assert(ALIGNED(addr, ASAN_ALIGN));
	assert(ALIGNED(size, ASAN_ALIGN));
	assert(addr >= ASAN_START);

	if(addr < ASAN_START || addr + size >= ASAN_END) {
		kpanic("asan: 0x%x - 0x%x", addr, addr + size);
	}

	if(!asan) {
		asan = true;
		asan_start = addr;
		asan_end = addr + size;
	}

	while(addr < ((tmp = atomic_load_relaxed(&asan_start)))) {
		if(!atomic_cmpxchg(&asan_start, tmp, addr)) {
			continue;
		}
	}

	while(addr + size > ((tmp = atomic_load_relaxed(&asan_end)))) {
		if(!atomic_cmpxchg(&asan_end, tmp, addr + size)) {
			continue;
		}
	}

	addr -= ASAN_START;

	/*
	 * Mark the range as being protected in the bitset.
	 */
	for(size_t i = 0; i < size; i += ASAN_ALIGN) {
		atomic_or_relaxed(&asan_buffer[ASAN_IDX(addr + i)],
			ASAN_BIT(addr + i));
	}
}

#include <kern/critical.h>
#include <kern/proc.h>

/**
 * @brief Check if a memory access is correct.
 */
static void asan_sanitize(vm_vaddr_t addr, vm_vsize_t size) {
	vm_vaddr_t start;

	if(kpanic_p()) {
		return;
	}

	/*
	 * XXX
	 * If compiled into the kernel, this function is getting called
	 * nearly everywhere and thus we temporarily check for low stack
	 * space here.
	 */
	if(!critsect_p()) {
		vm_vaddr_t stackptr = (vm_vaddr_t)&addr, stack;

		stack = (vm_vaddr_t)cur_thread()->kstack;
		if(stackptr > stack && stackptr - stack < 256) {
			kpanic("lostack");
		}
	}

	if(!asan || addr < asan_start || addr >= asan_end) {
		return;
	}

	start = ALIGN_DOWN(addr - ASAN_START, ASAN_ALIGN);
	for(size_t i = 0; i < size; i += ASAN_ALIGN) {
		if(atomic_load_relaxed(&asan_buffer[ASAN_IDX(start + i)])
			& ASAN_BIT(start + i))
		{
			asan = false;
			kpanic("[asan] error: address: 0x%x size: %d\n",
				addr, size);
		}
	}
}

#define define_asan(size) 					\
void __asan_load ## size (uintptr_t addr) {			\
	(void) addr;						\
}								\
void __asan_store ## size (uintptr_t addr) {			\
	asan_sanitize(addr, size);				\
}								\
void __asan_load ## size ## _noabort (uintptr_t addr) {		\
	(void) addr;						\
}								\
void __asan_store ## size ## _noabort (uintptr_t addr) {	\
	asan_sanitize(addr, size);				\
}								\
export(__asan_load ## size);					\
export(__asan_store ## size);					\
export(__asan_load ## size ## _noabort);			\
export(__asan_store ## size ## _noabort);

define_asan(1);
define_asan(2);
define_asan(4);
define_asan(8);
define_asan(16);

void __asan_storeN(uintptr_t addr, size_t size) {
	asan_sanitize(addr, size);
}
export(__asan_storeN);

void __asan_storeN_noabort(uintptr_t addr, size_t size) {
	__asan_storeN(addr, size);
}
export(__asan_storeN_noabort);

void __asan_loadN(uintptr_t addr, size_t size) {
	(void) addr;
	(void) size;
}
export(__asan_loadN);

void __asan_loadN_noabort(uintptr_t addr, size_t size) {
	(void) addr;
	(void) size;
}
export(__asan_loadN_noabort);

void __asan_handle_no_return(void) {
	return;
}
export(__asan_handle_no_return);

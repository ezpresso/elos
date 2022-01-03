#ifndef COMPILER_ASAN_H
#define COMPILER_ASAN_H

#include <config.h>

#define asan_prot(addr, size) \
	_asan_prot((vm_vaddr_t)(addr), (vm_vaddr_t)(size))

#define asan_rmprot(addr, size) \
	_asan_rmprot((vm_vaddr_t)(addr), (vm_vaddr_t)(size))

#if CONFIGURED(ASAN)
void _asan_prot(vm_vaddr_t addr, vm_vsize_t size);
void _asan_rmprot(vm_vaddr_t addr, vm_vsize_t size);
#else
static inline void _asan_prot(vm_vaddr_t addr, vm_vsize_t size) {
	(void) addr;
	(void) size;
}

static inline void _asan_rmprot(vm_vaddr_t addr, vm_vsize_t size) {
	(void) addr;
	(void) size;
}
#endif
#endif

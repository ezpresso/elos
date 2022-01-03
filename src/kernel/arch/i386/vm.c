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
#include <kern/multiboot.h>
#include <kern/init.h>
#include <kern/cpu.h>
#include <kern/sync.h>
#include <kern/critical.h>
#include <kern/sched.h>
#include <kern/percpu.h>
#include <lib/string.h>
#include <vm/vm.h>
#include <vm/phys.h>
#include <vm/vmem.h>
#include <vm/mmu.h>
#include <vm/layout.h>
#include <vm/page.h>
#include <vm/kern.h>
#include <arch/mp.h>
#include <config.h>

typedef struct vm_percpu {
	vm_vaddr_t pgcpy_src;
	vm_vaddr_t pgcpy_dst;
	sync_t quick_lock;
	vm_vaddr_t quick_map;
} vm_percpu_t;

extern vm_vaddr_t end;
extern vm_vaddr_t init_start_addr;
static DEFINE_PERCPU(vm_percpu_t, vm_percpu);
static vm_vaddr_t kern_end = (vm_vaddr_t)&end;

static vm_percpu_t *vm_percpu_get(void) {
	return PERCPU(&vm_percpu);
}

int vm_kern_map_phys_attr(vm_paddr_t addr, vm_vsize_t size, vm_flags_t flags,
	vm_memattr_t attr, void **out)
{
	vm_vaddr_t map_end;

	kassert(ALIGNED(addr, PAGE_SZ) && ALIGNED(size, PAGE_SZ),
		"[vm] map_phys_addr: invalid range: 0x%x - 0x%x", addr, size);

	if(init_get_level() == INIT_FINISHED) {
		/*
		 * init-memory is freed at this point in time, thus the
		 * mapping from &init_start_addr until &end cannot be used
		 * here.
		 */
		map_end = (vm_vaddr_t)&init_start_addr;
	} else {
		map_end = (vm_vaddr_t)&end;
	}

	if(addr + size <= map_end - KERNEL_VM_BASE &&
		attr == VM_MEMATTR_DEFAULT)
	{
		*out = (void *)addr + KERNEL_VM_BASE;
		return 0;
	} else {
		return vm_kern_generic_map_phys(addr, size, flags, attr, out);
	}
}

void vm_kern_unmap_phys(void *ptr, vm_vsize_t size) {
	vm_vaddr_t end = (vm_vaddr_t)&end;
	if(ptr >= (void *)&end) {
		vm_kern_generic_unmap_phys(ptr, size);
	}
}

void *vm_kern_map_phys_early(vm_paddr_t addr, vm_vsize_t size) {
	vm_vaddr_t map = (vm_vaddr_t)&end;

	kassert(ALIGNED(addr, PAGE_SZ) && ALIGNED(size, PAGE_SZ),
		"[vm] map_phys_early: invalid range: 0x%x - 0x%x", addr, size);

	if(addr + size <= map - KERNEL_VM_BASE) {
		return (void *)addr + KERNEL_VM_BASE;
	} else {
		map = kern_end;
		kern_end += size;

		mmu_map_kern(map, size, addr, VM_PROT_RW | VM_PROT_KERN,
			VM_MEMATTR_DEFAULT);
		return (void *)map;
	}
}

void vm_kern_unmap_phys_early(void *ptr, vm_vsize_t size) {
	vm_vaddr_t addr = (vm_vaddr_t)ptr;

	kassert(ALIGNED(addr, PAGE_SZ) && ALIGNED(size, PAGE_SZ),
		"[vm] unmap_phys_early: invalid range: 0x%x - 0x%x", addr, size);
	assert(addr + size == kern_end);

	kern_end = addr;
}

void *vm_mapdev(vm_paddr_t phys, vm_vsize_t size, vm_memattr_t attr) {
	size_t off = phys & ~PAGE_MASK;
	void *ptr;

	vm_kern_map_phys_attr(phys & PAGE_MASK, ALIGN(size + off, PAGE_SZ),
		VM_PROT_RW | VM_WAIT, attr, &ptr);

	return ptr + off;
}

void vm_unmapdev(void *ptr, vm_vsize_t size) {
	vm_kern_unmap_phys(ALIGN_PTR_DOWN(ptr, PAGE_SZ), size);
}

void *vm_kern_map_quick(vm_paddr_t phys) {
	vm_percpu_t *pcpu;

	kassert(ALIGNED(phys, PAGE_SZ), "[vm] map quick: unaligned "
		"address: 0x%x", phys);

	sched_pin();
	pcpu = vm_percpu_get();

	sync_acquire(&pcpu->quick_lock);
	mmu_map_kern(pcpu->quick_map, PAGE_SZ, phys, MMU_MAP_CPULOCAL |
			VM_PROT_KERN | VM_PROT_RW, VM_MEMATTR_DEFAULT);

	return (void *)pcpu->quick_map;
}

void vm_kern_unmap_quick(__unused void *ptr) {
	sync_release(&vm_percpu_get()->quick_lock);
	sched_unpin();
}

void vm_page_cpy_partial(vm_page_t *dst_page, vm_page_t *src_page,
	size_t size)
{
	vm_percpu_t *pcpu;
	vm_paddr_t dst, src;

	kassert(size <= PAGE_SZ, "[vm] page copy partial: invalid size: 0x%x",
		size);
	vm_page_assert_not_busy(src_page);

	dst = vm_page_phys(dst_page);
	src = vm_page_phys(src_page);

	critical_enter();
	pcpu = vm_percpu_get();

	/*
	 * TODO cache the current physical addresses of pgcpy regions.
	 */
	mmu_map_kern(pcpu->pgcpy_src, PAGE_SZ, src, MMU_MAP_CPULOCAL |
		VM_PROT_KERN | VM_PROT_RD, VM_MEMATTR_DEFAULT);
	mmu_map_kern(pcpu->pgcpy_dst, PAGE_SZ, dst, MMU_MAP_CPULOCAL |
		VM_PROT_KERN | VM_PROT_RW, VM_MEMATTR_DEFAULT);
	memcpy((void *)pcpu->pgcpy_dst, (void *)pcpu->pgcpy_src, size);

	/*
	 * Zero fill the rest of the page.
	 */
	if(size < PAGE_SZ) {
		memset((void *)pcpu->pgcpy_dst + size, 0x0, PAGE_SZ - size);
	}
	critical_leave();
}

void vm_init_cpu(void) {
	vm_percpu_t *pcpu = vm_percpu_get();

	sync_init(&pcpu->quick_lock, SYNC_MUTEX);

	pcpu->pgcpy_src = vmem_alloc(3 * PAGE_SZ, VM_WAIT);
	pcpu->pgcpy_dst = pcpu->pgcpy_src + PAGE_SZ;
	pcpu->quick_map = pcpu->pgcpy_dst + PAGE_SZ;
}

vm_paddr_t vm_kern_phys_end(void) {
	return (vm_paddr_t)&end - KERNEL_VM_BASE;
}

void __init vm_detect_mem(void) {
	int err;

	kprintf("[vm] detecting memory\n");

	/*
	 * Reserve the kernel binary.
	 */
	vm_phys_reserve(KERNEL_LOAD_ADDR, vm_kern_phys_end(), "kernel binary");

	/*
	 * Reserve ap-startup code.
	 */
#if CONFIGURED(MP)
	vm_phys_reserve(AP_CODE_ADDR, PAGE_SZ, "ap-cpu startup code");
#endif

	/*
	 * Reserve the modules loaded by the bootloader and register
	 * usable physical memory.
	 */
	err = multiboot_init_mem();
	if(err) {
		kpanic("[vm] no memory map information available");
	}

	kprintf("[vm] memory detection done\n");
}

void __init vm_vmem_init(void) {
	/*
	 * Start up the kernel's virtual memory manager.
	 */
	vmem_init(kern_end, VMEM_END);
}

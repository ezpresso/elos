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
#include <kern/init.h>
#include <vm/kern.h>
#include <vm/vas.h>
#include <vm/layout.h>
#include <vm/vmem.h>
#include <vm/object.h>
#include <vm/mmu.h>

vm_vas_t vm_kern_vas;

static int vm_kern_vas_map(vm_vas_t *vas, vm_vsize_t size, vm_map_t *map) {
	vm_vaddr_t addr;

	assert(!F_ISSET(map->flags, VM_MAP_32));
	addr = vmem_alloc(size, VM_WAIT);
	mman_insert(&vas->mman, addr, size, &map->node);

	return 0;
}

static void vm_kern_vas_unmap(vm_vas_t *vas, vm_vaddr_t addr, vm_vaddr_t size) {
	(void) vas;
	mmu_unmap_kern(addr, size);
	vmem_free(addr, size);
}

static void vm_kern_map_fixed(vm_vas_t *vas, vm_vaddr_t addr, vm_vsize_t size,
	vm_map_t *map)
{
	(void) vas;
	(void) addr;
	(void) size;
	(void) map;
	kpanic("[vm] kern: no fixed kernel mappings");
}

int vm_kern_generic_map_phys(vm_paddr_t addr, vm_vsize_t size,
	vm_flags_t flags, vm_memattr_t attr, void **out)
{
	vm_vaddr_t virt;
	int err;

	VM_FLAGS_CHECK(flags, VM_PROT_RW | VM_WAIT);
	virt = vmem_alloc(size, flags & VM_WAIT);
	if(virt == VMEM_ERR_ADDR) {
		return -ENOMEM;
	}

	err = mmu_map_kern(virt, size, addr, flags | VM_PROT_KERN, attr);
	if(err) {
		vmem_free(virt, size);
		return err;
	} else {
		return *out = (void *)virt, 0;
	}
}

void vm_kern_generic_unmap_phys(void *ptr, vm_vsize_t size) {
	vm_vaddr_t addr = (vm_vaddr_t)ptr;

	mmu_unmap_kern(addr, size);
	vmem_free(addr, size);
}

int vm_kern_map_object(vm_object_t *object, vm_vsize_t size,
	vm_objoff_t off, vm_flags_t flags, void **out)
{
	/*
	 * TODO not necessarily true, but...
	 */
	kassert(VM_PROT_KERN_P(flags), "[vm] mapping non-kernel memory"
		" into kernelspace");

	return vm_vas_map(&vm_kern_vas, VM_MAP_ANY, size, object, off, flags,
		flags & VM_PROT_RWX, out);
}

void vm_kern_unmap_object(void *addr, vm_vsize_t size) {
	int err = vm_vas_unmap(&vm_kern_vas, (vm_vaddr_t)addr, size);
	kassert(!err, "[vm] unmapping kernel memory failed");
}

int vm_kern_map_page(struct vm_page *page, vm_flags_t flags, void **out) {
	vm_vaddr_t addr;
	int err;

	VM_FLAGS_CHECK(flags, VM_PROT_RW | VM_WAIT);
	addr = vmem_alloc(PAGE_SZ, flags & VM_WAIT);
	if(addr == VMEM_ERR_ADDR) {
		return -ENOMEM;
	}

	err = mmu_map_page(&vm_kern_vas.mmu, addr, page, flags | VM_PROT_KERN);
	if(err) {
		vmem_free(addr, PAGE_SZ);
	} else {
		*out = (void *)addr;
	}

	return err;
}

void vm_kern_unmap_page(void *ptr) {
	vm_vaddr_t addr = (vm_vaddr_t) ptr;

	mmu_unmap_kern(addr, PAGE_SZ);
	vmem_free(addr, PAGE_SZ);
}

static vm_vas_funcs_t vm_kern_funcs = {
	.map = vm_kern_vas_map,
	.map_fixed = vm_kern_map_fixed,
	.unmap = vm_kern_vas_unmap,
};

void __init vm_kern_init(void) {
	vm_vas_init(&vm_kern_vas, KERNEL_VM_START, KERNEL_VM_END,
		&vm_kern_funcs);
}

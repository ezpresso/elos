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
#include <kern/proc.h>
#include <kern/signal.h>
#include <vm/vm.h>
#include <vm/layout.h>
#include <vm/vmem.h>
#include <vm/phys.h>
#include <vm/mmu.h>
#include <vm/malloc.h>
#include <vm/vas.h>
#include <vm/kern.h>
#include <vm/pghash.h>
#include <vm/page.h>
#include <vm/object.h>
#include <vm/shpage.h>
#include <vm/pressure.h>
#include <vm/pageout.h>
#include <vm/slab.h>

vm_page_t *vm_zero_page;
void *vm_zero_map;
vm_init_t vm_init = 0;

int vm_fault(vm_vaddr_t addr, vm_flags_t flags) {
	vm_object_t *object = NULL;
	vm_map_t *map = NULL;
	vm_vas_t *vas;
	vm_page_t *page;
	int err;

	VM_FLAGS_CHECK(flags, VM_PROT_RW | VM_PROT_USER | VM_PROT_KERN);
	kassert(F_ISSET(flags, VM_PROT_RW) &&
		((flags & VM_PROT_RW) != VM_PROT_RW), "[vm] fault: invalid "
		" flags: %d", flags);

#if 0
	kprintf("[vm] fault: ");
	if(flags & VM_PROT_RD) {
		kprintf("r");
	}
	if(flags & VM_PROT_WR) {
		kprintf("w");
	}
	kprintf(", thread: %d\n", cur_thread()->tid);
#endif

	if(VM_IS_KERN(addr)) {
		/*
		 * The user should not access the kernel.
		 */
		if(VM_PROT_USER_P(flags)) {
			return -EPERM;
		}

		vas = &vm_kern_vas;
	} else {
		if(VM_PROT_KERN_P(flags)) {
			/*
			 * The kern may only access user memory using copyin,
			 * copyout and other methods provided by kern/user.h.
			 */
			if(!thread_mayfault()) {
				return -EPERM;
			}

			kassert(thread_mayfault(), "[vm] fault: the kernel is "
				"trying to access user memory without "
				"copyin/out: 0x%x", addr);
		}

		vas = vm_vas_current;
	}

	/*
	 * The lower bits of the address can be ignored.
	 */
	addr &= PAGE_MASK;
	for(;;) {
		vm_flags_t map_prot;

		err = vm_vas_fault(vas, addr, flags, &map, &object);
		if(err) {
			return err;
		}

		map_prot = VM_FLAGS_PROT(map->flags);
		err = vm_object_fault(object, vm_map_addr_offset(map, addr),
			flags, &map_prot, &page);
		sync_release(&object->lock);
		if(err) {
			goto error;
		}

#if 0
		kprintf("[vm] fault: mapping page at 0x%x (0x%x, 0x%x)\n",
			addr, vm_page_phys(page), map_prot);
#endif
		err = mmu_map_page(&vas->mmu, addr, page, map_prot);
		vm_page_unpin(page);
		if(err) {
			assert(err == -ENOMEM);
			goto error;
		}

		vm_vas_fault_done(map);
		return 0;

error:
		vm_vas_fault_done(map);
		if(err == -ENOMEM) {
			/*
			 * Always wait after vm_vas_lookup_done was called,
			 * because otherwise the lock would be held the whole
			 * time waiting.
			 */
			vm_mem_wait(VM_PR_MEM_PHYS, PAGE_SZ);
			continue;
		} else {
			return err;
		}
	}

	notreached();
}

void vm_sigsegv(void) {
	int err;

	err = kern_tkill_cur(SIGSEGV);
	if(err) {
		kern_exitproc(0, SIGSEGV);
	}
}

static __init void vm_init_zero_map(void) {
	vm_vaddr_t addr;

	vm_zero_page = vm_page_alloc(VM_WAIT);
	addr = vmem_alloc(PAGE_SZ, VM_WAIT);

	vm_page_zero(vm_zero_page);
	mmu_map_kern(addr, PAGE_SZ, vm_page_phys(vm_zero_page), VM_PROT_KERN |
		VM_PROT_RD, VM_MEMATTR_DEFAULT);
	vm_zero_map = (void *)addr;
}

void __init init_vm(void) {
	vm_detect_mem();

	/*
	 * Start up the bootstrap physical memory allocator.
	 */
	vm_phys_init_early();

	/*
	 * Start up the kernel's virtual memory manager.
	 */
	vm_vmem_init();
	vm_init_cpu();

	/*
	 * Start up the physical memory allocator.
	 */
	vm_phys_init();

	vm_slab_init();
	vm_malloc_init();
	vm_pghash_init();
	vm_kern_init();
	vm_init_zero_map();
	vm_pageout_init();
	vm_shpage_init();
}

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
#include <vm/vas.h>
#include <vm/object.h>
#include <vfs/file.h>
#include <vfs/proc.h>
#include <sys/mman.h>

#define MMAP_FLAGS (MAP_SHARED | MAP_PRIVATE | MAP_FIXED | MAP_ANON | \
		MAP_32BIT | MAP_NORESERVE | MAP_GROWSDOWN | MAP_EXECUTABLE | \
		MAP_LOCKED | MAP_STACK)

#define MMAP_PROT (PROT_EXEC | PROT_READ | PROT_WRITE)

#define MMAP_WARN (MAP_32BIT | MAP_NORESERVE | MAP_GROWSDOWN | MAP_LOCKED | \
		MAP_STACK)

static int vm_prot_flags(int prot, vm_flags_t *result) {
	if((prot & ~MMAP_PROT) != 0) {
		return -EINVAL;
	} else {
		*result = 0;
		*result |= (prot & PROT_READ) ? VM_PROT_RD : 0;
		*result |= (prot & PROT_WRITE) ? VM_PROT_WR : 0;
		*result |= (prot & PROT_EXEC) ? VM_PROT_EXEC : 0;

		if((MMU_MAP_NO_WO && (prot & (PROT_WRITE | PROT_READ)) ==
			PROT_WRITE))
		{
			*result |= VM_PROT_RD;
		}

		return 0;
	}
}

static int vm_mmap_flags(int prot, int flags, vm_flags_t *out) {
	vm_flags_t res;
	int err;

	/*
	 * Validate the map flags.
	 */
	if(((flags & ~MMAP_FLAGS) != 0) ||
		(flags & (MAP_SHARED | MAP_PRIVATE)) == 0)
	{
		return -EINVAL;
	} else if(flags & MMAP_WARN) {
		kprintf("[mmap] warning: flags currently not implemented: "
			"0x%x\n", flags & MMAP_WARN);
		return -EINVAL;
	}

	err = vm_prot_flags(prot, &res);
	if(err) {
		return err;
	}

	res |= (flags & MAP_SHARED) ? VM_MAP_SHARED : 0;
	res |= (flags & MAP_FIXED) ? VM_MAP_FIXED : 0;

	/*
	 * The size of the mapping might be unaligned. TODO remove this flag.
	 */
	res |= VM_MAP_UNALIGNED;

	*out = res;
	return 0;
}

intptr_t sys_mmap2(void *ptr, size_t length, int prot, int flags, int fd,
	unsigned long pgoffset)
{
	vm_flags_t vm_flags, max_prot;
	vm_object_t *object;
	vm_objoff_t offset;
	vm_vaddr_t addr;
	void *result;
	int err;

	addr = (vm_vaddr_t)ptr;
	offset = (vm_objoff_t)pgoffset << PAGE_SHIFT;
	if(length == 0 || !ALIGNED(addr, PAGE_SZ)) {
		return -EINVAL;
	} else if(VM_OBJOFF_MAX - offset < length) {
		/*
		 * Not sure, man-pages only talk abut 32 bit systems here,
		 * but the uint64 can overflow too!?
		 */
		return -EOVERFLOW;
	}

	err = vm_mmap_flags(prot, flags, &vm_flags);
	if(err) {
		return err;
	}

	if(!F_ISSET(flags, MAP_ANONYMOUS)) {
		file_t *file = fdget(fd);

		err = file_mmap(file, offset, length, &vm_flags, &max_prot,
			&object);
		file_unref(file);
		if(err) {
			return err;
		}
	} else {
		/*
		 * The offset should be zero for anonymous objects.
		 */
		if(pgoffset) {
			return -EINVAL;
		}

		max_prot = VM_PROT_RW;
		object = vm_anon_alloc(length, VM_ZERO);
	}

	err = vm_vas_map(vm_vas_current, (vm_vaddr_t)addr, length, object,
		offset, vm_flags, max_prot, &result);
	vm_object_unref(object);
	if(err) {
		return err;
	} else {
		return (intptr_t) result;
	}
}

int sys_munmap(uintptr_t addr, size_t length) {
	if(!ALIGNED(addr, PAGE_SZ) || !ALIGNED(length, PAGE_SZ) ||
		length == 0)
	{
		return -EINVAL;
	} else {
		return vm_vas_unmap(vm_vas_current, addr, length);
	}
}

int sys_brk(__unused uintptr_t addr) {
	return -ENOSYS;
}

int sys_mprotect(uintptr_t addr, size_t len, int prot) {
	vm_flags_t flags;
	int err;

	if(!ALIGNED(addr, PAGE_SZ) || !ALIGNED(len, PAGE_SZ)) {
		return -EINVAL;
	}

	err = vm_prot_flags(prot, &flags);
	if(err) {
		return err;
	}

	if(len == 0) {
		return 0;
	} else {
		return vm_vas_protect(vm_vas_current, addr, len, flags);
	}
}

int sys_madvise(uintptr_t addr, size_t length, int advice) {
	(void) addr;
	(void) length;
	(void) advice;
	return 0;
}

#if 0
int sys_mlock(void *addr, size_t len) {

}

int sys_munlock(void *addr, size_t len) {

}

int sys_msync(void *addr, size_t length, int flags) {

}

int sys_mlockall(__unused int flags) {
	return -ENOSYS;
}

int sys_munlockall(void) {
	return -ENOSYS;
}
#endif

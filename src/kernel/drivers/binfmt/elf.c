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
 * purpose with or without fee is hereby granted, proided that the above
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
#include <kern/exec.h>
#include <kern/stack.h>
#include <kern/proc.h>
#include <kern/random.h>
#include <kern/init.h>
#include <vm/malloc.h>
#include <vm/object.h>
#include <vm/vas.h>
#include <vfs/vfs.h>
#include <vfs/vnode.h>
#include <vfs/lookup.h>
#include <vfs/uio.h>
#include <lib/elf.h>
#include <lib/string.h>
#include <arch/elf.h>
#include <sys/limits.h>

#ifndef ELF_WORD_SIZE
#error ELF_WORD_SIZE not defined
#endif

#define ELF_SYM(name)		CONCAT(elf_, CONCAT(name, ELF_WORD_SIZE))
#define ELF_TYPE(name)		CONCAT(CONCAT(elf, ELF_WORD_SIZE), _##name##_t)
#define elf_hdr_t		ELF_TYPE(ehdr)
#define elf_phdr_t		ELF_TYPE(phdr)
#define elf_addr_t		ELF_TYPE(addr)

#define elf_check_machine	ELF_SYM(check_machine)
#define elf_check_header	ELF_SYM(check_header)
#define elf_check_flags		ELF_SYM(check_flags)
#define elf_load_section	ELF_SYM(load_section)
#define elf_vm_prot		ELF_SYM(vm_prot)
#define elf_exec		ELF_SYM(exec)
#define elf_initaux		ELF_SYM(initaux)
#define elf_binfmt		ELF_SYM(binfmt)

static void elf_vm_prot(elf_phdr_t *phdr, vm_flags_t *flags) {
	*flags = 0;
	*flags |= (phdr->p_flags & PF_R) ? VM_PROT_RD : 0;
	*flags |= (phdr->p_flags & PF_W) ? VM_PROT_WR : 0;
	*flags |= (phdr->p_flags & PF_X) ? VM_PROT_EXEC : 0;
}

/**
 * @brief Load a segment from an elf-file, described by a program header
 */
static int elf_load_section(exec_img_t *image, elf_phdr_t *phdr,
	uintptr_t base)
{
	vm_vaddr_t file_addr, end, anon_addr;
	vm_vsize_t file_size, anon_size;
	vm_object_t *object;
	vm_flags_t prot;
	vm_objoff_t off;
	size_t pgoff;
	int err;

	if(phdr->p_filesz > phdr->p_memsz) {
		return -ENOEXEC;
	} else if(phdr->p_memsz == 0) {
		return 0;
	}

	/*
	 * The real end of the mapping (including the file
	 * backed part and the anonymous part).
	 */
	end = ALIGN(phdr->p_vaddr + phdr->p_memsz, PAGE_SZ);

	/*
	 * The address of the file backed part.
	 */
	file_addr = phdr->p_vaddr & PAGE_MASK;

	/*
	 * The offset into the executable file.
	 */
	off = phdr->p_offset & PAGE_MASK;
	pgoff = phdr->p_vaddr & ~PAGE_MASK;

	/*
	 * Example: addr = 0x804af40; off = 0x1f40;
	 */
	if((phdr->p_offset & ~PAGE_MASK) != pgoff) {
		return -ENOEXEC;
	}

	/*
	 * If the memory size is greater than the file size there will be two
	 * consecutiv mappings. The first mapping maps parts of the executable
	 * file and the following mapping is an anonymous one. There is a
	 * problem since parts of the executable mapping and parts of the
	 * anonymous mapping may be in the same page, which is technically not
	 * supported by the vm because the vm always works with page-aligned
	 * stuff. However the vm supports unaligned object sizes and as long
	 * as the address is aligned, the size of a vm-map may be unaligned
	 * see vm_map_t->real_size).
	 */
	if(phdr->p_filesz) {
		file_size = phdr->p_filesz + pgoff;

		/*
		 * If the file-size and the memory size are equal, we do not
		 * need an unaligned size for the mapping, which simplifies
		 * things for the vm.
		 */
		if(phdr->p_filesz == phdr->p_memsz) {
			file_size = ALIGN(file_size, PAGE_SZ);
		}
	} else {
		file_size = 0;
	}

	elf_vm_prot(phdr, &prot);

	/*
	 * Map the executable into the address space of the process. Remeber
	 * that vm_vas_map supports an unaligned size and the elf-section sizes
	 * may be unaligned. The vm zeroes the rest of the last page in a
	 * mapping with an unaligned size, which is exactly the behaviour we
	 * want here.
	 */
	if(file_size != 0) {
		vm_flags_t flags = prot | VM_PROT_USER | VM_MAP_FIXED |
			VM_MAP_UNALIGNED;

		/*
		 * Don't shadow the vnode object, if one can not write to the
		 * mapping in any circumstance (see max_prot for vm_vas_map).
		 * However if parts of the last page have to zeroed
		 * (memsz > filesz), a shadow object is needed nonetheless
		 * (remember that the code above aligned file_size to the page
		 * size if memsz == filesz).
		 *
		 * TODO vm_vas_map could clear the SHADOW flag if it detects
		 * such a situation!?
		 */
		if(VM_PROT_WR_P(prot) || !ALIGNED(file_size, PAGE_SZ)) {
			flags |= VM_MAP_SHADOW;
		}

		object = VNTOVM(image->node);
		err = vm_vas_map(image->vas, file_addr + base, file_size,
			object, off, flags, prot, NULL);
		if(err) {
			goto error;
		}
	}

	if(phdr->p_memsz > phdr->p_filesz) {
		anon_addr = file_addr + ALIGN(file_size, PAGE_SZ);
		anon_size = end - anon_addr;
	} else {
		/*
		 * Either no bss in this segment, or the bss is rather small and
		 * resides in the previous mapping (see the comments above).
		 */
		anon_size = 0;
	}

	if(anon_size != 0) {
		object = vm_anon_alloc(anon_size, VM_ZERO);
		err = vm_vas_map(image->vas, anon_addr + base, anon_size,
			object, 0, prot | VM_PROT_USER | VM_MAP_FIXED, prot,
			NULL);
		vm_object_unref(object);
		if(err) {
			goto error;
		}
	}

	return 0;

error:
	if(err != -ENOMEM) {
		return -ENOEXEC;
	} else {
		return err;
	}
}

/**
 * @brief Validate an elf header.
 *
 * Remember that it's the caller's task to check for the elf
 * magic value.
 *
 * @retval true The header is valid.
 * @retval false The header is erroneous.
 */
static bool elf_check_header(elf_hdr_t *header) {
	return elf_check_machine(header->e_machine) == true &&
		elf_check_flags(header->e_flags) == true &&
		header->e_ident[EI_CLASS] == ELF_CLASS &&
		header->e_ident[EI_DATA] == ELF_DATA &&
		header->e_version <= EV_CURRENT &&
		header->e_phentsize == sizeof(elf_phdr_t) &&
		header->e_phnum > 0 &&
		header->e_phoff < PAGE_SZ &&
		ALIGNED(header->e_phoff, sizeof(elf_addr_t)) &&
		header->e_phentsize * header->e_phnum + header->e_phoff
			<= PAGE_SZ;
}

/**
 * @brief Load an elf interpreter.
 *
 * Load an elf interpreter, which itself is a dynamic elf file.
 */
static int elf_load_interp(exec_img_t *pimage, const char *interp,
	uintptr_t max, uintptr_t *base)
{
	elf_hdr_t *header;
	exec_img_t image;
	elf_phdr_t *phdr;
	bool was_exe;
	int err;

	err = exec_init_img(&image, interp);
	if(err) {
		goto err0;
	}

	err = exec_lock_map_node(&image);
	if(err) {
		goto err0;
	}

	header = image.header;
	image.vas = pimage->vas;

	/*
	 * Validate the header.
	 */
	if(memcmp(header->e_ident, ELFMAG, SELFMAG) ||
		elf_check_header(header) == false || header->e_type != ET_DYN ||
		header->e_entry == 0)
	{
		err = -ENOEXEC;
		goto err1;
	}

	was_exe = vnode_set_exe(image.node);

	/*
	 * The interpreter is loaded at the beginning of the addres space to
	 * simplify things. _max_ is the lowest address of the real executable
	 * program. Thus the interpreter has to fit inide the region staring
	 * at the beginnig of the address space and _max_.
	 */
	*base = vm_vas_start(pimage->vas);
	phdr = image.header + header->e_phoff;

	/*
	 * Load the relevant sections of the interpreter.
	 */
	for(size_t i = 0; i < header->e_phnum; i++) {
		if(phdr[i].p_type != PT_LOAD) {
			continue;
		}

		if(phdr[i].p_vaddr >= max || max - phdr[i].p_vaddr <
			phdr[i].p_memsz)
		{
			err = -ENOEXEC;
			goto err2;
		}

		err = elf_load_section(&image, phdr + i, *base);
		if(err) {
			goto err2;
		}
	}

	pimage->entry = header->e_entry + *base;
	err = 0;

err2:
	if(err && was_exe == false) {
		vnode_unset_exe(image.node);
	}
err1:
	vnode_unlock(image.node);
err0:
	/*
	 * Make sure that exec_cleanup does not try to free image.vas.
	 */
	image.vas = NULL;
	exec_cleanup(&image);
	return err;
}

/**
 * @brief Try to execute an elf file.
 */
static int elf_exec(exec_img_t *image) {
	elf_hdr_t *header = image->header;
	uintptr_t interp_max = 0, base;
	char *interp = NULL;
	elf_phdr_t *phdr;
	size_t nseg = 0;
	bool was_exe;
	int err;

	if(memcmp(header->e_ident, ELFMAG, SELFMAG)) {
		return EXEC_NOMAG;
	}

	/*
	 * Validate the elf header.
	 */
	if(elf_check_header(header) == false || header->e_entry == 0 ||
		(header->e_type != ET_EXEC && header->e_type != ET_DYN))
	{
		return -ENOEXEC;
	}

	if(header->e_type == ET_DYN) {
		base = vm_vas_start(image->vas);
	} else {
		base = 0;
	}

	/*
	 * The executable seems to be valid, so let's mark it as being an
	 * executable, which will prevent subsequent writes to the vnode.
	 * If an error occurs, we can simply call vnode_unset_exe, because
	 * the vnode is read-locked all the time.
	 */
	was_exe = vnode_set_exe(image->node);

	image->aux.phnum = header->e_phnum;
	image->aux.entry = header->e_entry + base;
	image->aux.phdr = 0;

	phdr = image->header + header->e_phoff;
	for(size_t i = 0; i < header->e_phnum; i++) {
		switch(phdr[i].p_type) {
		case PT_LOAD:
			err = elf_load_section(image, &phdr[i], base);
			if(err) {
				goto error;
			}

			if(phdr[i].p_offset <= header->e_phoff &&
				phdr[i].p_offset + phdr[i].p_filesz >
				header->e_phoff)
			{
				image->aux.phdr = (phdr[i].p_vaddr & PAGE_MASK)
					+ header->e_phoff + base;
			}

			if(nseg == 0 || phdr[i].p_vaddr < interp_max) {
				interp_max = (phdr[i].p_vaddr & PAGE_MASK)
					+ base;
			}

			nseg++;
			break;
		case PT_INTERP:
			/*
			 * TODO currently no interpreter here
			 * Second TODO: what did I want to say with the comment
			 * above?
			 */
			if(header->e_type == ET_DYN) {
				err = -ENOEXEC;
				goto error;
			}

			/*
			 * There cannot be more than one interpreter.
			 */
			if(interp != NULL || phdr[i].p_filesz > PATH_MAX) {
				return -ENOEXEC;
			} else if(phdr[i].p_offset < PAGE_SZ &&
				PAGE_SZ - phdr[i].p_offset >= phdr[i].p_filesz)
			{
				/*
				 * How convenient, the path to the interpreter
				 * was already mapped when mapping the header of
				 * the executable.
				 */
				interp = image->header + phdr[i].p_offset;
			} else {
				struct iovec iov;
				ssize_t size;
				uio_t uio;

				kpanic("TODO TESTTHIS");

				/*
				 * Allocate some space for the path to the
				 * interpreter.
				 */
				interp = kmalloc(phdr[i].p_filesz + 1, VM_WAIT);

				/*
				 * Setup the uio structure needed by vnode_read.
				 */
				iov.iov_base = interp;
				iov.iov_len = phdr[i].p_filesz;
				uio_simple(&uio, &iov, phdr[i].p_offset,
					UIO_KERN, UIO_RD);

				/*
				 * Read the path to the interpreter. Remember
				 * that image->node is currently locked.
				 */
				size = vnode_read(image->node, &uio);
				if(size >= 0 &&
					(size_t)size != phdr[i].p_filesz)
				{
					/*
					 * This may happen if the file is too
					 * small.
					 */
					err = -ENOEXEC;
					goto error;
				} else {
					err = size;
					goto error;
				}

				/*
				 * Terminate the path.
				 */
				interp[phdr[i].p_filesz] = '\0';
			}
			break;
		default:
			break;
		}
	}

	/*
	 * Don't allow an application to have zero sections.
	 */
	if(nseg == 0 || image->aux.phdr == 0) {
		err = -ENOEXEC;
		goto error;
	}

	if(interp != NULL) {
		uintptr_t interp_base;

		if(interp_max == 0) {
			err = -ENOEXEC;
			goto error;
		}

		/*
		 * TODO this is a little bit tricky, because writes to the file
		 * may spuriously fail (vnode_set_exe) when unlocking the node,
		 * even if the exec() fails.
		 */
		vnode_unlock(image->node);
		err = elf_load_interp(image, interp, interp_max, &interp_base);
		vnode_lock(image->node, VNLOCK_SHARED);
	} else {
		image->entry = header->e_entry + base;
		err = EXEC_OK;
	}

error:
	if(interp != NULL && (interp < (char *)image->header ||
		interp >= (char *)(image->header + PAGE_SZ)))
	{
		kfree(interp);
	}

	if(err && was_exe == false) {
		vnode_unset_exe(image->node);
	}

	return err;
}

/**
 * @brief Copyout an aux value to the new stack of the executable.
 */
static inline int elf_copyout_aux(kstack_t *stack, long key, long value) {
	int retv = stack_copyout_val(stack, value);
	if(retv == 0) {
		retv = stack_copyout_val(stack, key);
	}
	return retv;
}

/**
 * @brief Copy the auxvals to the stack.
 */
static int elf_initaux(exec_img_t *image, kstack_t *stack) {
	proc_t *proc = cur_proc();
	void *binary, *urand;
	char random[16];
	int err;

	/*
	 * Copy the string for AT_EXECFN to the stack.
	 */
	err = stack_copyout(stack, image->binary, strlen(image->binary) + 1,
		&binary);
	if(err) {
		return err;
	}

	/*
	 * Get 16 random bytes for AT_RANDOM.
	 */
	for(size_t i = 0; i < AT_RANDOM_NUM; i++) {
		random[i] = krand();
	}

	/*
	 * Copy the 16 random bytes to the stack.
	 */
	err = stack_copyout(stack, random, AT_RANDOM_NUM, &urand);
	if(err) {
		return err;
	}

	/*
	 * Put the auxvals on the stack.
	 */
#define elf_aux(s, k, v) { err = elf_copyout_aux(s, k, v); if(err) return err; }
	elf_aux(stack, AT_NULL, 0);
	elf_aux(stack, AT_PHDR, image->aux.phdr);
	elf_aux(stack, AT_PHENT, sizeof(elf32_phdr_t));
	elf_aux(stack, AT_PHNUM, image->aux.phnum);
	elf_aux(stack, AT_PAGESZ, PAGE_SZ);
	elf_aux(stack, AT_BASE, vm_vas_start(image->vas));
	elf_aux(stack, AT_FLAGS, 0);
	elf_aux(stack, AT_ENTRY, image->aux.entry);
	elf_aux(stack, AT_UID, proc->uid);
	elf_aux(stack, AT_EUID, proc->euid);
	elf_aux(stack, AT_GID, proc->gid);
	elf_aux(stack, AT_EGID, proc->egid);
	elf_aux(stack, AT_SECURE, 0); /* TODO */
	elf_aux(stack, AT_RANDOM, (uintptr_t)urand);
	elf_aux(stack, AT_EXECFN, (uintptr_t)binary);
#undef elf_aux

	return 0;
}

static binfmt_t elf_binfmt = {
	.name = "elf" STR(ELF_WORD_SIZE),
	.exec = elf_exec,
	.initaux = elf_initaux,
};

static __init int ELF_SYM(init) (void) {
	binfmt_register(&elf_binfmt);
	return INIT_OK;
}

late_initcall(ELF_SYM(init));

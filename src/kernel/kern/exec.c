
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
#include <kern/exec.h>
#include <kern/user.h>
#include <kern/proc.h>
#include <kern/stack.h>
#include <kern/symbol.h>
#include <vfs/lookup.h>
#include <vfs/vnode.h>
#include <vfs/vfs.h>
#include <vm/object.h>
#include <vm/vas.h>
#include <vm/malloc.h>
#include <vm/kern.h>
#include <vm/page.h>
#include <vm/shpage.h>
#include <sys/limits.h>
#include <lib/string.h>

static DEFINE_LIST(binfmt_list);
static rwlock_t binfmt_lock = RWLOCK_INIT;

/**
 * Allocate memory for the argument vector and the environment variables.
 */
static int exec_alloc_mem(exec_img_t *image) {
	vm_object_t *object;
	int err;

	/*
	 * Allocate demand paged memory. The anonymous object is not zero
	 * filled.
	 *
	 * TODO make sure no swap happens (otherwise kernel could generate a
	 * dangerous page fault due to IO error, which cannot be handled
	 * correctly)
	 */
	object = vm_anon_alloc(ARG_MAX, VM_NOFLAG);
	err = vm_kern_map_object(object, ARG_MAX, 0x0,
		VM_PROT_RW | VM_PROT_KERN, &image->strmem);
	vm_object_unref(object);
	if(err == 0) {
		image->strspace = ARG_MAX;
		image->strptr = image->strmem;
	}

	return err;
}

/**
 * @brief Map the first page of the executable.
 */
static int exec_map_header(exec_img_t *image) {
	vm_page_t *page;
	int err = 0;

	assert(image->header == NULL);
	synchronized(&VNTOVM(image->node)->lock) {
		err = vnode_getpage(image->node, 0, VM_PROT_RD, &page);
	}
	if(err) {
		return err;
	}

	vm_kern_map_page(page, VM_WAIT | VM_PROT_RD, &image->header);
	image->page = page;
	return 0;
}

/**
 * @brief Unmap the first page of the executable if it was mapped.
 */
static void exec_unmap_header(exec_img_t *image) {
	if(image->header) {
		vm_kern_unmap_page(image->header);
		vm_page_unpin(image->page);
		image->header = NULL;
	}
}

int exec_init_img(exec_img_t *image, const char *path) {
	image->binary = path;
	image->flags = 0;
	image->strmem = NULL;
	image->header = NULL;
	image->page = NULL;
	image->vas = NULL;
	image->node = NULL;

	return vlookup_node(NULL, image->binary, 0, &image->node);
}

void exec_cleanup(exec_img_t *image) {
	exec_unmap_header(image);
	if(image->node != NULL) {
		vnode_unref(image->node);
	}

	if(image->strmem != NULL) {
		vm_kern_unmap_object(image->strmem, ARG_MAX);
	}

	if(image->vas != NULL) {
		vm_user_vas_free(image->vas);
	}

	if(image->flags & EXEC_FREEPATH) {
		kfree(DECONST(char *, image->binary));
	}
}

int exec_lock_map_node(exec_img_t *image) {
	int err;

	vnode_lock(image->node, VNLOCK_SHARED);
	err = vnode_check_exe(image->node);
	if(err == 0) {
		err = exec_map_header(image);
	}

	if(err) {
		vnode_unlock(image->node);
	}

	return err;
}

int exec_interp(exec_img_t *image, const char *interp, size_t len) {
	char *cpy;
	int err;

	/*
	 * Copy the interpreter path to a temporary buffer, because
	 * it's likely that interp is a pointer from the image->header
	 * mapping and we're unmapping it afterwards.
	 */
	cpy = kmalloc(len + 1, VM_NOFLAG);
	if(cpy == NULL) {
		return -ENOMEM;
	}

	memcpy(cpy, interp, len);
	cpy[len] = '\0';

	vnode_unlock(image->node);
	exec_unmap_header(image);
	if(image->node != NULL) {
		vnode_unref(image->node);
		image->node = NULL;
	}

	if(image->flags & EXEC_FREEPATH) {
		kfree(DECONST(char *, image->binary));
	}

	image->binary = cpy;
	image->flags |= EXEC_FREEPATH;

	err = vlookup_node(NULL, image->binary, 0, &image->node);
	if(err) {
		return err;
	}

	err = exec_lock_map_node(image);
	if(err) {
		vnode_unref(image->node);
		image->node = NULL;
		return err;
	}

	return EXEC_INTERP;
}

/**
 * Create an array of pointers in user memory, pointing to the individual
 * strings in @p str.
 */
static int exec_copyout_vec(kstack_t *stack, const char *str, size_t nstr,
	const char *ustr, const char ***out)
{
	char **array;
	int err;

	/*
	 * Reserve space on the spack for the array. The '+1' is for the last
	 * NULL entry.
	 */
	array = stack_rsv_align(stack, ((nstr + 1) * sizeof(char *)),
		__alignof__(char *));
	if(array == NULL) {
		return -ENOSPC;
	}

	/*
	 * Copy the strings to userspace.
	 */
	for(size_t i = 0; i < nstr; i++) {
		err = copyout(array + i, &ustr, sizeof(char *));
		assert(err == 0);
		while(*str++ != '\0') {
			ustr++;
		}
		ustr++;
	}

	/*
	 * String vectors end with a NULL entry.
	 */
	ustr = NULL;
	err = copyout(array + nstr, &ustr,  sizeof(char *));
	assert(err == 0);

	*out = (const char **)array;
	return 0;
}

/**
 * Allocate and initialize the stack with the argument-vector, the
 * environment variables and optionally the aux-vector.
 */
static int exec_init_stack(exec_img_t *image, binfmt_t *binfmt) {
	vm_object_t *object;
	void *uargs, *uenv;
	kstack_t stack;
	void *map;
	int err;

	/* TODO stack and exec prot!!! */

	/*
	 * Allocate some space for a stack.
	 */
	object = vm_anon_alloc(VM_STACK_SIZE, VM_ZERO);
	err = vm_vas_map(image->vas, VM_STACK_ADDR, VM_STACK_SIZE, object, 0,
		VM_PROT_RWX | VM_PROT_USER | VM_MAP_FIXED, VM_PROT_RWX, &map);
	vm_object_unref(object);
	if(err) {
		return err;
	}

	stack_init(&stack, map, VM_STACK_SIZE);

	/*
	 * Copy the environment strings to userspace.
	 */
	err = stack_copyout(&stack, image->env, image->envsize, &uenv);
	if(err) {
		goto error;
	}

	/*
	 * Copy the argument strings to userspace.
	 */
	err = stack_copyout(&stack, image->args, image->argsize, &uargs);
	if(err) {
		goto error;
	}

	/*
	 * Construct the aux array.
	 */
	if(binfmt->initaux) {
		err = binfmt->initaux(image, &stack);
		if(err) {
			goto error;
		}
	}

	/*
	 * Construct the environment array.
	 */
	err = exec_copyout_vec(&stack, image->env, image->envc, uenv,
		&image->envvec);
	if(err) {
		goto error;
	}

	/*
	 * Construct the argument vector.
	 */
	err = exec_copyout_vec(&stack, image->args, image->argc, uargs,
		&image->argvec);
	if(err) {
		goto error;
	}

	err = stack_copyout_val(&stack, image->argc);
	image->stackptr = stack_pointer(&stack);

error:
	/*
	 * If the allocated stack was too small, claim that args and env
	 * were too long.
	 */
	if(err == -ENOSPC) {
		err = -E2BIG;
	} else if(err) {
		kpanic("[exec] unexpected error: %d", err);
	}

	return err;
}

/**
 * Copy the strings from a user string vector into the image's buffer.
 */
static int exec_copyin_vec(exec_img_t *image, const char **vec, vm_seg_t seg,
	size_t *out, size_t *size)
{
	size_t num = 0, length;
	void *ptr, *old = image->strptr;
	int err;

	for(;;) {
		if(seg == KERNELSPACE) {
			ptr = (void *)*vec;
		} else {
			err = copyin(&ptr, vec, sizeof(void *));
			if(err) {
				return err;
			}
		}

		/*
		 * Stop when the string vector terminator was reached.
		 */
		if(ptr == NULL) {
			break;
		} else if(image->strspace == 0) {
			return -E2BIG;
		}

		/*
		 * Copy the string into the buffer.
		 */
		if(seg == KERNELSPACE) {
			length = strlcpy(image->strptr, ptr, image->strspace);
			if(length >= image->strspace) {
				return -E2BIG;
			}
		} else {
			err = copyinstr(image->strptr, ptr, image->strspace,
				&length);
			if(err == -ENAMETOOLONG) {
				/*
				 * Return the error "Arg list too long" in
				 * this case.
				 */
				return -E2BIG;
			} else if(err) {
				return err;
			}
		}

		/*
		 * Count the string terminator byte.
		 */
		length++;
		image->strptr += length;
		image->strspace -= length;
		vec++;
		num++;
	}

	*out = num;
	*size = image->strptr - old;

	return 0;
}

/**
 * @brief Copy argv and env into a kernel buffer.
 */
static int exec_copyin(exec_img_t *image, const char **argv,
	const char **env, vm_seg_t seg)
{
	int err;

	if(argv == NULL || env == NULL) {
		return -EFAULT;
	}

	/*
	 * Allocate a big buffer of demand paged memory.
	 */
	err = exec_alloc_mem(image);
	if(err) {
		return err;
	}

	/*
	 * Copy the argument strings into the buffer.
	 */
	image->args = image->strptr;
	err = exec_copyin_vec(image, argv, seg, &image->argc, &image->argsize);
	if(err) {
		/*
		 * The caller will free the memory allocated by exec_alloc_mem.
		 */
		return err;
	}

	/*
	 * Copy the environment strings into the buffer.
	 */
	image->env = image->strptr;
	return exec_copyin_vec(image, env, seg, &image->envc, &image->envsize);
}

int kern_execve(const char *path, const char **argv, const char **envp,
	vm_seg_t seg)
{
	proc_t *proc = cur_proc();
	proc_image_t *procimg;
	vm_vas_t *vas, *old;
	binfmt_t *binfmt;
	exec_img_t image;
	size_t pathlen;
	int err;

	/*
	 * Initialize the image structure and lookup the path.
	 */
	err = exec_init_img(&image, path);
	if(err) {
		goto err0;
	}

	/*
	 * Copy argv and envp into a kernel buffer.
	 */
	err = exec_copyin(&image, argv, envp, seg);
	if(err) {
		goto err0;
	}

	/*
	 * Enter single threadding mode, but do not kill the other threads
	 * yet, the execve() might still fail.
	 */
	err = proc_singlethread(PROC_ST_WAIT);
	if(err) {
		goto err0;
	}

	/*
	 * Allocate a new virtual address space and switch to it. The old
	 * virtual address space is not freed until the exec is done.
	 * Changing the vas of a process is possible, because the other
	 * threads in the process are currently not running.
	 */
	vas = vm_user_vas_alloc();
	image.vas = vas;

	/*
	 * Map the first page of the node.
	 */
	err = exec_lock_map_node(&image);
	if(err) {
		goto err1;
	}

	/*
	 * Switch to the new virtual address space.
	 */
	old = proc->vas;
	proc->vas = vas;
	vm_vas_switch(vas);

	/*
	 * Start loading the executable.
	 */
	rdlock(&binfmt_lock);
	err = -ENOEXEC;

	do {
		foreach(binfmt, &binfmt_list) {
			err = binfmt->exec(&image);
			if(err == EXEC_NOMAG) {
				err = -ENOEXEC;
				continue;
			} else {
				break;
			}
		}
	} while(err == EXEC_INTERP);

	/*
	 * The executable does not have a known format.
	 */
	if(err < 0) {
		rwunlock(&binfmt_lock);
		goto err2;
	} else {
		assert(err == EXEC_OK);
	}

	/*
	 * The binary has been mmaped into the memory space
	 * of the process, so the node is not needed anymore.
	 */
	vnode_unlock(image.node);

	err = exec_init_stack(&image, binfmt);
	binfmt = NULL; /* for safety */
	rwunlock(&binfmt_lock);
	if(err) {
		goto err2;
	}

	err = vm_shpage_map(image.vas);
	if(err) {
		goto err2;
	}

	/*
	 * The other threads of the process are currently waiting in
	 * thread_uret(), let's kill them.
	 */
	proc_singlethread(PROC_ST_KILL);

	/*
	 * Clear pending signals, close CLOEXEC fds, ...
	 */
	proc_exec();

	/*
	 * Reset registers and set new IP and SP.
	 */
	arch_uthread_setup(cur_thread(), image.entry,
		(uintptr_t)image.stackptr);

	/*
	 * Free the old vmctx. Remember that vm_vas_unmap can only unmap
	 * from the currently active vas.
	 */
	proc->vas = old;
	vm_vas_switch(old);
	vm_vas_unmap(old, vm_vas_start(old), vm_vas_size(old));
	proc->vas = image.vas;
	vm_vas_switch(proc->vas);
	vm_user_vas_free(old);;

	pathlen = strlen(path);
	procimg = kmalloc(sizeof(proc_image_t) + pathlen + 1, VM_WAIT);
	ref_init(&procimg->ref);
	memcpy(procimg->binary, path, pathlen);
	procimg->binary[pathlen] = '\0';
	proc_set_image(proc, procimg);

	/*
	 * Free the resources allocated during execve(). 'image.vas' is set to
	 * NULL in order to keep exec_cleanup from freeing it.
	 */
	image.vas = NULL;
	exec_cleanup(&image);

	return 0;

err2:
	/*
	 * Unmap everything in the new address space, while the address space
	 * is still loaded.
	 */
	vm_vas_unmap(vas, vm_vas_start(vas), vm_vas_size(vas));

	/*
	 * Switch back to the old virtual address space.
	 */
	proc->vas = old;
	vm_vas_switch(old);

	/*
	 * image.node may be NULL if the binfmt-driver called exec_interp
	 * and exec_interp() failed while mapping the interpreter header.
	 */
	if(image.node) {
		vnode_unlock(image.node);
	}
err1:
	/*
	 * Wakeup the other threads again.
	 */
	proc_singlethread(PROC_ST_END);
err0:
	exec_cleanup(&image);
	return err;
}

int sys_execve(const char *upath, const char **uargv, const char **uenvp) {
	char *path;
	int err;

	err = copyin_path(upath, &path);
	if(err == 0) {
		err = kern_execve(path, uargv, uenvp, USERSPACE);
		kfree(path);
	}

	return err;
}

void binfmt_register(binfmt_t *binfmt) {
	list_node_init(binfmt, &binfmt->node);

	wrlock_scope(&binfmt_lock);
	list_append(&binfmt_list, &binfmt->node);
}
export(binfmt_register);

void binfmt_unregister(binfmt_t *binfmt) {
	wrlocked(&binfmt_lock) {
		list_remove(&binfmt_list, &binfmt->node);
	}

	list_node_destroy(&binfmt->node);
}
export(binfmt_unregister);

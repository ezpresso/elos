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
#include <kern/proc.h>
#include <kern/rwlock.h>
#include <lib/string.h>
#include <vfs/proc.h>
#include <vfs/vpath.h>
#include <vfs/file.h>
#include <vfs/vfs.h>

typedef struct vfs_proc {
	sync_t lock;
	vpath_t cwd;
	vpath_t root;
	mode_t umask;
	rwlock_t fdlock;

	/* pointer to files + CLOEXEC flag */
#define FLAG_CLOEXEC (1 << 0)
	uintptr_t files[PROC_FILES];
} vfs_proc_t;

#define fd_file(pls, fd) ((file_t *)((pls)->files[fd] & ~FLAG_CLOEXEC))
#define fd_invalid(fd)	 unlikely((fd) < 0 || (fd) >= PROC_FILES)

static int vfs_proc_init(proc_t *proc);
static int vfs_proc_fork(proc_t *dst, proc_t *src);
static void vfs_proc_exec(proc_t *proc);
static void vfs_proc_exit(proc_t *proc);

static define_pls(vfs_proc_local) = {
	.size = sizeof(vfs_proc_t),
	.init = vfs_proc_init,
	.fork = vfs_proc_fork,
	.exec = vfs_proc_exec,
	.exit = vfs_proc_exit,
};

static inline vfs_proc_t *vfs_pls(proc_t *proc) {
	if(proc == NULL) {
		proc = cur_proc();
	}

	assert(proc != &kernel_proc);
	return pls_get_proc(proc, &vfs_proc_local);
}

static int vfs_proc_init(proc_t *proc) {
	vfs_proc_t *pls = vfs_pls(proc);

	sync_init(&pls->lock, SYNC_MUTEX);
	rwlock_init(&pls->fdlock);
	pls->umask = 022;

	/*
	 * The file table of other processes is initialized in fork.
	 */
	if(!proc_was_forked(proc)) {
		memset(pls->files, 0x00, sizeof(pls->files));
		vpath_cpy(&pls->root, &vfs_root);
		vpath_cpy(&pls->cwd, &vfs_root);
	}

	return 0;
}

static int vfs_proc_fork(proc_t *dst, proc_t *src) {
	vfs_proc_t *dst_pls = vfs_pls(dst), *src_pls = vfs_pls(src);
	file_t *file;

	dst_pls->umask = src_pls->umask;
	synchronized(&src_pls->lock) {
		vpath_cpy(&dst_pls->cwd, &src_pls->cwd);
		vpath_cpy(&dst_pls->root, &src_pls->root);
	}

	rdlock_scope(&src_pls->fdlock);
	for(int i = 0; i < PROC_FILES; i++) {
		file = fd_file(src_pls, i);
		if(file != NULL) {
			file_ref(file);
		} else {
			assert(!F_ISSET(src_pls->files[i], FLAG_CLOEXEC));
		}

		dst_pls->files[i] = src_pls->files[i];
	}

	return 0;	
}

static void vfs_proc_exec(proc_t *proc) {
	vfs_proc_t *pls = vfs_pls(proc);
	file_t *file;

	wrlock_scope(&pls->fdlock);
	for(int i = 0; i < PROC_FILES; i++) {
		file = fd_file(pls, i);

		/*
		 * Close CLOEXEC and directory descriptors.
		 */
		if(file && (pls->files[i] & FLAG_CLOEXEC ||
			file->type == FDIR)) 
		{
			file_unref(fd_file(pls, i));
			pls->files[i] = 0;
		}
	}
}

static void vfs_proc_exit(proc_t *proc) {
	vfs_proc_t *pls = vfs_pls(proc);

	vpath_clear(&pls->root);
	vpath_clear(&pls->cwd);

	wrlock(&pls->fdlock);
	for(int i = 0; i < PROC_FILES; i++) {
		if(pls->files[i]) {
			file_unref(fd_file(pls, i));
		}
	}
	rwunlock(&pls->fdlock);

	sync_destroy(&pls->lock);
	rwlock_destroy(&pls->fdlock);
}

void vfs_get_root(vpath_t *path) {
	if(cur_proc() == &kernel_proc) {
		vpath_cpy(path, &vfs_root);
	} else {
		vfs_proc_t *pls = vfs_pls(NULL);

		/*
		 * Make sure that vpath_cpy does not call
		 * vnode_unref / vmount_unref while pls->lock
		 * is locked.
		 */
		vpath_clear(path);

		sync_scope_acquire(&pls->lock);
		vpath_cpy(path, &pls->root);
	}
}

void vfs_set_root(vpath_t *path) {
	vfs_proc_t *pls  = vfs_pls(NULL);
	vpath_t old = VPATH_INIT;

	synchronized(&pls->lock) {
		old = pls->root;
		pls->root.node = vnode_ref(path->node);
		pls->root.mount = vmount_ref(path->mount);
	}

	vpath_clear(&old);
}

void vfs_get_cwd(vpath_t *path) {
	if(cur_proc() == &kernel_proc) {
		vpath_cpy(path, &vfs_root);
	} else {
		vfs_proc_t *pls  = vfs_pls(NULL);

		/*
		 * Make sure that vpath_cpy does not call
		 * vnode_unref.
		 */
		vpath_clear(path);

		sync_scope_acquire(&pls->lock);
		vpath_cpy(path, &pls->cwd);
	}
}

void vfs_set_cwd(vpath_t *path) {
	vfs_proc_t *pls  = vfs_pls(NULL);
	vpath_t old = VPATH_INIT;

	synchronized(&pls->lock) {
		old = pls->cwd;
		pls->cwd.node = vnode_ref(path->node);
		pls->cwd.mount = vmount_ref(path->mount);
	}

	vpath_clear(&old);
}

bool vfs_is_root(vpath_t *path) {
	if(cur_proc() == &kernel_proc) {
		return vfs_root.node == path->node &&
			vfs_root.mount == path->mount;
	} else {
		vfs_proc_t *pls = vfs_pls(NULL);
	
		sync_scope_acquire(&pls->lock);
		return pls->root.node == path->node &&
			pls->root.mount == path->mount;
	}
}

mode_t vfs_get_umask(void) {
	if(cur_proc() == &kernel_proc) {
		return 0;
	} else { 
		vfs_proc_t *pls = vfs_pls(NULL);
		return atomic_load_relaxed(&pls->umask);
	}
}

mode_t vfs_set_umask(mode_t mask) {
	vfs_proc_t *pls = vfs_pls(NULL);
	assert(cur_proc() != &kernel_proc);
	return atomic_xchg_relaxed(&pls->umask, mask & 0777);
}

int fdfree(int fd) {
	vfs_proc_t *pls = vfs_pls(NULL);
	file_t *file = NULL;

	if(fd_invalid(fd)) {
		return -EBADF;
	}

	wrlocked(&pls->fdlock) {
		file = fd_file(pls, fd);
		pls->files[fd] = 0;
	}

	if(file) {
		file_unref(file);
		return 0;
	} else {
		return -EBADF;
	}
}

static int fdalloc_intern(vfs_proc_t *pls, file_t *file, bool cloexec,
	int min)
{
	int res = -ENFILE;

	if(min < 0 || min >= PROC_FILES) {
		return -EINVAL;
	}

	/*
	 * Check if pointer is properly aligned, so that it
	 * fit can fit the cloexec flag.
	 */
	assert(!F_ISSET((uintptr_t)(file), FLAG_CLOEXEC));

	for(int i = min; i < PROC_FILES; i++) {
		if(pls->files[i] == 0) {
			pls->files[i] = (uintptr_t)file_ref(file);
			if(cloexec) {
				F_SET(pls->files[i], FLAG_CLOEXEC);
			}

			res = i;
			break;
		}
	}

	if(res >= 0) {
		assert(fd_file(pls,res)->fops != NULL);
	}

	return res;
}

int fdalloc(file_t *file, bool cloexec, int min) {
	vfs_proc_t *pls = vfs_pls(NULL);

	wrlock_scope(&pls->fdlock);
	return fdalloc_intern(pls, file, cloexec, min);
}

int fdalloc2(file_t *files[2], bool cloexec, int fd[2]) {
	vfs_proc_t *pls = vfs_pls(NULL);

	wrlock_scope(&pls->fdlock);
	fd[0] = fdalloc_intern(pls, files[0], cloexec, 0);
	if(fd[0] < 0) {
		return fd[0];
	}

	fd[1] = fdalloc_intern(pls, files[1], cloexec, 0);
	if(fd[1] < 0) {
		pls->files[fd[0]] = 0;
		file_unref(files[0]);
		return fd[1];
	}

	return 0;
}

int fddup(file_t *file, int newfd) {
	vfs_proc_t *pls = vfs_pls(NULL);
	file_t *old = NULL;
	uintptr_t value;

	if(fd_invalid(newfd)) {
		return -EBADF;
	}

	value = (uintptr_t)file_ref(file);
	wrlocked(&pls->fdlock) {
		/*
		 * Remove the old file descriptor and set the file
		 * descriptor to the file
		 */
		old = fd_file(pls, newfd);
		pls->files[newfd] = value;
	}

	if(old) {
		file_unref(old);
	}

	return newfd;
}

file_t *fdget(int fd) {
	vfs_proc_t *pls = vfs_pls(NULL);
	file_t *file;

	if(fd_invalid(fd)) {
		return NULL;
	}

	rdlock_scope(&pls->fdlock);
	file = fd_file(pls, fd);
	if(file != NULL) {
		file_ref(file);
	}

	return file;
}

int fd_cloexec_get(int fd) {
	vfs_proc_t *pls = vfs_pls(NULL);

	if(fd_invalid(fd)) {
		return -EBADF;
	}

	wrlock_scope(&pls->fdlock);
	if(pls->files[fd] == 0) {
		return -EBADF;
	} else {
		return pls->files[fd] & FLAG_CLOEXEC;
	}
}

int fd_cloexec_set(int fd, bool cloexec) {
	vfs_proc_t *pls = vfs_pls(NULL);

	if(fd_invalid(fd)) {
		return -EBADF;
	}

	wrlock_scope(&pls->fdlock);
	if(pls->files[fd] == 0) {
		return -EBADF;
	} else if(cloexec) {
		pls->files[fd] |= FLAG_CLOEXEC;
	} else {
		pls->files[fd] &= ~FLAG_CLOEXEC;
	}

	return 0;
}

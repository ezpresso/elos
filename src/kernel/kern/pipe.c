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
#include <kern/signal.h>
#include <kern/atomic.h>
#include <kern/user.h>
#include <kern/time.h>
#include <kern/proc.h>
#include <vm/malloc.h>
#include <vfs/file.h>
#include <vfs/proc.h>
#include <vfs/uio.h>
#include <lib/ringbuf.h>
#include <sys/stat.h>

#define PIPE_SIZE 512

typedef struct pipe {
#define PIPE_READ 	(1 << 0)
#define PIPE_WRITE 	(1 << 1)
	int closed;
	ringbuf_t buf;

	struct timespec time;
} pipe_t;

static pipe_t *pipe_alloc(void) {
	pipe_t *pipe;
	int err;

	pipe = kmalloc(sizeof(*pipe), VM_NOFLAG);
	if(!pipe) {
		return NULL;
	}

	err = ringbuf_alloc(&pipe->buf, PIPE_SIZE);
	if(err) {
		kfree(pipe);
		return NULL;
	}

	pipe->closed = 0; 
	return pipe;
}

static void pipe_free(pipe_t *pipe) {
	ringbuf_free(&pipe->buf);
	kfree(pipe);
}

static int pipe_open(__unused file_t *file) {
	kpanic("[pipe] error: called pipe_open()\n");
}

static void pipe_close(file_t *file) {
	pipe_t *pipe = file_get_priv(file);
	int cflag;

	ringbuf_eof(&pipe->buf);

	if(FWRITEABLE(file->flags)) {
		cflag = PIPE_WRITE;
	} else {
		cflag = PIPE_READ;
	}

	/*
	 * If both ends are closed, free the pipe.
	 */
	if(atomic_or_relaxed(&pipe->closed, cflag) & ~cflag) {
		pipe_free(pipe);
	}
}

static int pipe_rb_flags(file_t *file) {
	return F_ISSET(file->flags, O_NONBLOCK) ? RB_NOBLOCK : 0;
}

static ssize_t pipe_read(file_t *file, uio_t *uio) {
	pipe_t *pipe = file_get_priv(file);
	return ringbuf_read_uio(&pipe->buf, uio, pipe_rb_flags(file));
}

static ssize_t pipe_write(file_t *file, uio_t *uio) {
	pipe_t *pipe = file_get_priv(file);
	int err, closed = atomic_load_relaxed(&pipe->closed);

	if(closed & PIPE_READ) {
		kern_tkill_cur(SIGPIPE);
		return -EPIPE;
	}

	err = ringbuf_write_uio(&pipe->buf, uio, pipe_rb_flags(file));
	if(err == -EPIPE && !signal_is_ignored(SIGPIPE)) {
		err = kern_tkill_cur(SIGPIPE);
		if(err != 0) {
			err = -EPIPE;
		}
	}

	return err;
}

static int pipe_stat(file_t *file, struct stat64 *stat) {
	pipe_t *pipe = file_get_priv(file);
	proc_t *proc = cur_proc();

#if notyet
	if(pipe->named) {
		/* TODO stat->st_blksize = PIPE_SIZE; even for vnode */
		return vnode_fops.stat(file, stat);
	}
#endif

	assert(sizeof(stat->st_ino) >= sizeof(pipe));
	stat->st_ino = (ino_t) (uintptr_t) pipe;

	stat->st_rdev = 0;
	stat->st_dev = 0;
	stat->st_nlink = 0;
	stat->st_blksize = PIPE_SIZE;
	stat->st_blocks = 1;
	stat->st_mode = S_IFIFO;
	stat->st_size = PIPE_SIZE;
	stat->st_atim = pipe->time;
	stat->st_mtim = pipe->time;
	stat->st_ctim = pipe->time;

	synchronized(&proc->lock) {
		stat->st_uid = proc->uid;
		stat->st_gid = proc->gid;
	}

	return 0;
}

static fops_t pipe_ops = {
	.open = pipe_open,
	.close = pipe_close,
	.write = pipe_write,
	.read = pipe_read,
	.stat = pipe_stat,
	.seek = NULL, /* No seek for pipe */
};

int sys_pipe2(int usr_pipefd[2], int flags) {
	bool cloexec = false;
	file_t *files[2];
	int pipefd[2];
	int file_flags;
	pipe_t *pipe;
	int err;

	if(flags & O_CLOEXEC) {
		cloexec = true;
	}

	if(flags & O_NONBLOCK) {
		file_flags = O_NONBLOCK;
	} else {
		file_flags = 0;
	}

	pipe = pipe_alloc();
	if(!pipe) {
		err = -ENOMEM;
		goto err_pipe;
	}

	err = file_alloc(FPIPE, file_flags | O_RDONLY, &pipe_ops, &files[0]);
	if(err) {
		goto err_rd_file;
	}

	err = file_alloc(FPIPE, file_flags | O_WRONLY, &pipe_ops, &files[1]);
	if(err) {
		goto err_wr_file;
	}

	file_set_priv(files[0], pipe);
	file_set_priv(files[1], pipe);

	/*
	 * Atomically allocate 2 files.
	 */
	err = fdalloc2(files, cloexec, pipefd);
	if(err) {
		goto err_fd;
	}

	err = copyout(usr_pipefd, pipefd, sizeof(pipefd));
	if(err) {
		goto err_cpyout;
	}

	file_unref(files[0]);
	file_unref(files[1]);
	realtime(&pipe->time);

	return 0;

err_cpyout:
	/*
	 * Technically another thread might have closed and
	 * reused those file descriptors by the time, but
	 * when is a serious application going to close
	 * a filedesc not yet returned by a syscall?
	 */
	fdfree(pipefd[0]);
	fdfree(pipefd[1]);
err_fd:
	file_unref(files[1]);
err_wr_file:
	file_unref(files[0]);
err_rd_file:
	pipe_free(pipe);
err_pipe:
	return err;
}

int sys_pipe(int fd[2]) {
	return sys_pipe2(fd, 0);
}

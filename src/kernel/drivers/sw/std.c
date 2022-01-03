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
#include <vfs/dev.h>
#include <vfs/file.h>
#include <vfs/proc.h>
#include <sys/stat.h>
#include <sys/unistd.h>

/*
 * TODO this is quick and dirty.
 */
#define stderr_fop(op, args...) ({		\
	file_t *stderr;				\
	int err;				\
	stderr = fdget(STDERR_FILENO);		\
	if(!stderr) {				\
		err = -EBADF;			\
	} else {				\
		err = op(stderr, ## args);	\
		file_unref(stderr);		\
	}					\
	err;					\
})

static int stderr_open(file_t *file) {
	if(FREADABLE(file->flags)) {
		return -EINVAL;
	} else {
		return 0;
	}
}

static int stderr_write(__unused file_t *file, uio_t *uio) {
	return stderr_fop(file_write, uio);
}

static fops_t stderr_ops = {
	.open = stderr_open,
	.write = stderr_write,
	/* TODO */
};

static __init int std_dev_init(void) {
	/*
	 * Create /dev/stderr.
	 */
	int err = makechar(NULL, MAJOR_MEM, 0666, &stderr_ops, NULL, NULL,
		"stderr");
	if(err) {
		return INIT_PANIC;
	} else {
		return INIT_OK;
	}
}

fs_initcall(std_dev_init);
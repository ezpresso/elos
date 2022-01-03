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

/*
 * The driver for the /dev/tty device
 */

#include <kern/system.h>
#include <kern/init.h>
#include <kern/proc.h>
#include <kern/tty.h>
#include <vfs/file.h>
#include <vfs/dev.h>
#include <sys/stat.h>

#define RETV ret =
#define NORETV
#define ttydev_call(x, func, ...) ({				\
	tty_t *tty = session_get_tty();				\
	int ret = 0;						\
	if(tty == NULL) {					\
		ret = -ENXIO;					\
	} else {						\
		x tty->driver->func(tty, ## __VA_ARGS__);	\
		tty_unref(tty);					\
	}							\
	ret;							\
})

static dev_t ttydev;

static int ttydev_open(__unused file_t *file) {
	return ttydev_call(RETV, open);
}

static void ttydev_close(__unused file_t *file) {
	ttydev_call(NORETV, close);
}

static ssize_t ttydev_read(__unused file_t *file, uio_t *uio) {
	return ttydev_call(RETV, read, uio);
}

static ssize_t ttydev_write(__unused file_t *file, uio_t *uio) {
	return ttydev_call(RETV, write, uio);
}

static int ttydev_ioctl(__unused file_t *file, int cmd, void *arg) {
	return ttydev_call(RETV, ioctl, cmd, arg);
}

static fops_t ttydev_ops = {
	.open = ttydev_open,
	.close = ttydev_close,
	.read = ttydev_read,
	.write = ttydev_write,
	.ioctl = ttydev_ioctl,
};

static __init int ttydev_init(void) {
	int err;

	/*
	 * Create /dev/tty file.
	 */
	err = makechar(NULL, MAJOR_TTY, 0666, &ttydev_ops, NULL, &ttydev,
		"tty");
	if(err) {
		return INIT_PANIC;
	} else {
		return INIT_OK;
	}
}

dev_initcall(ttydev_init);

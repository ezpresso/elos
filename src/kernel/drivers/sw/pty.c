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
#include <kern/tty.h>
#include <kern/init.h>
#include <kern/signal.h>
#include <kern/user.h>
#include <vm/malloc.h>
#include <vfs/file.h>
#include <vfs/uio.h>
#include <vfs/dev.h>
#include <vfs/devfs.h>
#include <lib/bitset.h>
#include <sys/stat.h>

#define PTY_MAX	32

typedef struct pty {
	tty_t tty;
	int num;
	dev_t slave_dev;
} pty_t;

static fop_open_t ptmx_open;
static fop_close_t ptmx_close;
static fop_read_t ptmx_write;
static fop_read_t ptmx_read;
static fop_ioctl_t ptmx_ioctl;
static fops_t ptmx_ops = {
	.open = ptmx_open,
	.close = ptmx_close,
	.read = ptmx_read,
	.write = ptmx_write,
	.ioctl = ptmx_ioctl,
};

static tty_open_t pts_open;
static tty_close_t pts_close;
static tty_ioctl_t pty_ioctl;
static tty_free_t pty_free;
static tty_start_t pts_start;
static tty_driver_t pts_driver = {
	.open = pts_open,
	.close = pts_close,
	.read = tty_ldisc_read,
	.write = tty_ldisc_write,
	.ioctl = pty_ioctl,
	.free = pty_free,
	.start = pts_start,
};

static sync_t pty_lock = SYNC_INIT(MUTEX);
static struct devfs_node *pts_dir;
static bset_t pty_numbers;

static void pty_free(tty_t *tty) {
	pty_t *pty = tty->priv;

	synchronized(&pty_lock) {
		bset_free_bit(&pty_numbers, pty->num);
	}

	tty_uninit(tty);
	kfree(pty);
}

static int pts_open(__unused tty_t *tty) {
	return 0;
}

static void pts_close(__unused tty_t *tty) {
	return;
}

static inline void pts_start(tty_t *tty) {
	tty_wakeup(tty, &tty->obuf);
}

static int pty_ioctl(tty_t *tty, int cmd, void *arg) {
	pty_t *pty = tty->priv;
	int err;

	switch(cmd) {
	case TIOCSPTLCK: {
		int id;

		err = copyin(&id, arg, sizeof(id));
		if(err) {
			return err;
		}

		if(id != 0) {
			return -ENOTSUP;
		}
	}
	/* FALLTHROUGH */
	case TIOCGPTN:
		return copyout(arg, &pty->num, sizeof(pty->num));
	default:
		return tty_ioctl(tty, cmd, arg);
	}
}

static int ptmx_open(__unused file_t *file) {
	int num = 0, err;
	pty_t *pty;

	/*
	 * Allocate a slave number.
	 */
	synchronized(&pty_lock) {
		num = bset_alloc_bit(&pty_numbers);
	}
	if(num < 0) {
		return -ENODEV;
	}

	pty = kmalloc(sizeof(*pty), VM_WAIT);
	tty_init(&pty->tty, &pts_driver, pty);
	tty_default(&pty->tty);
	pty->num = num;

	/*
	 * Allocate a slave device file for this file.
	 */
	err = tty_makedev(pts_dir, &pty->tty, "%d", pty->num);
	if(err) {
		tty_unref(&pty->tty);
		return err;
	}

	file_set_priv(file, &pty->tty);

	/*
	 * tty_dev_create added a reference to the tty, which
	 * will keep it alive.
	 */
	tty_unref(&pty->tty);

	return 0;
}

static void ptmx_close(file_t *file) {
	tty_t *tty = file_get_priv(file);

	/*
	 * Destroy the tty if the master side is being
	 * closed.
	 */
	tty_destroy(tty);
	file_set_priv(file, NULL);
}

static ssize_t ptmx_read(file_t *file, uio_t *uio) {
	tty_t *tty = file_get_priv(file);
	size_t done = 0;
	uint8_t buf[128];
	int err;

	sync_acquire(&tty->lock);
	err = tty_wait_data(tty, &tty->obuf);
	if(err) {
		sync_release(&tty->lock);
		return err;
	}

	while(uio->size && !cbuf_is_empty(&tty->obuf)) {
		ssize_t res;
		size_t cur;

		cur = cbuf_read(&tty->obuf, min(uio->size, sizeof(buf)), buf);
		if(cur == 0) {
			break;
		}

		/*
		 * Now there is some space in the output buffer again, so
		 * let's wakeup the ones waiting for space.
		 */
		tty_wakeup(tty, &tty->obuf);
		sync_release(&tty->lock);

		res = uiomove(buf, cur, uio);
		if(res < 0) {
			return res;
		}

		done += res;
		sync_acquire(&tty->lock);
	}

	sync_release(&tty->lock);
	return done;
}

static ssize_t ptmx_write(file_t *file, uio_t *uio) {
	tty_t *tty = file_get_priv(file);
	size_t done = 0;
	char buf[128];

	while(uio->size) {
		ssize_t ret;

		ret = uiomove(buf, sizeof(buf), uio);
		if(ret < 0) {
			return ret;
		}

		sync_acquire(&tty->lock);
		for(size_t i = 0; i < (size_t)ret; i++) {
			tty->ldisc->input(tty, buf[i]);
		}
		sync_release(&tty->lock);

		done += ret;
	}

	return done;
}

static int ptmx_ioctl(file_t *file, int cmd, void *arg) {
	tty_t *tty = file_get_priv(file);
	return pty_ioctl(tty, cmd, arg);
}

static __init int pty_init(void) {
	int err;

	err = bset_alloc(&pty_numbers, PTY_MAX);
	if(err) {
		return INIT_PANIC;
	}

	pts_dir = devfs_mkdir(NULL, "pts");
	err = makechar(NULL, MAJOR_TTY, 0666, &ptmx_ops, NULL, NULL, "ptmx");
	if(err) {
		return INIT_PANIC;
	}

	return INIT_OK;
}

dev_initcall(pty_init);

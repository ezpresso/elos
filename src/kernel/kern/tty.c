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
#include <kern/proc.h>
#include <kern/signal.h>
#include <kern/user.h>
#include <kern/futex.h>
#include <vm/malloc.h>
#include <vfs/file.h>
#include <vfs/uio.h>
#include <vfs/dev.h>
#include <lib/ascii.h>
#include <sys/stat.h>
#include <sys/limits.h>

static fop_open_t tty_fop_open;
static fop_close_t tty_fop_close;
static fop_read_t tty_fop_read;
static fop_write_t tty_fop_write;
static fop_ioctl_t tty_fop_ioctl;
static fops_t tty_fops = {
	.open = tty_fop_open,
	.close = tty_fop_close,
	.read = tty_fop_read,
	.write = tty_fop_write,
	.ioctl = tty_fop_ioctl,
};

tty_t *tty_ref(tty_t *tty) {
	ref_inc(&tty->ref);
	return tty;
}

void tty_unref(tty_t *tty) {
	if(ref_dec(&tty->ref)) {
		assert(F_ISSET(tty->flags, TTY_DEAD));
		tty->driver->free(tty);
	}
}

void tty_init(tty_t *tty, tty_driver_t *driver, void *priv) {
	sync_init(&tty->lock, SYNC_MUTEX);
	ref_init(&tty->ref);

	tty->priv = priv;
	tty->driver = driver;
	tty->session = 0;
	tty->fg_pgrp = 0;
	tty->dev = 0;
	tty->flags = 0;

	tty->column = 0;
	cbuf_alloc(&tty->obuf, TTY_BUFSIZE, VM_WAIT);
	cbuf_alloc(&tty->ibuf, TTY_BUFSIZE, VM_WAIT);
	tty->canbuf = kmalloc(TTY_BUFSIZE, VM_WAIT);
	tty->candata = 0;

	/*
	 * TODO
	 */
	extern tty_ldisc_t termios_ldisc;
	tty->ldisc = &termios_ldisc;
}

void tty_default(tty_t *tty) {
	tty->termios.c_cc[VEOF]	= 4;	/* ^D */
	tty->termios.c_cc[VEOL]	= 0;	/* Not set */
	tty->termios.c_cc[VERASE] = '\b';
	tty->termios.c_cc[VINTR] = 3;	/* ^C */
	tty->termios.c_cc[VKILL] = 21;	/* ^U */
	tty->termios.c_cc[VMIN] = 1;
	tty->termios.c_cc[VQUIT] = 28;	/* ^\ */
	tty->termios.c_cc[VSTART] = 17;	/* ^Q */
	tty->termios.c_cc[VSTOP] = 19;	/* ^S */
	tty->termios.c_cc[VSUSP] = 26;	/* ^Z */
	tty->termios.c_cc[VTIME] = 0;
	tty->termios.c_iflag = ICRNL | BRKINT;
	tty->termios.c_oflag = ONLCR | OPOST | ONLRET;
	tty->termios.c_lflag = ECHO | ECHOE | ECHOK | ICANON | ISIG | IEXTEN;
	tty->termios.c_cflag = CREAD;
	tty->winsize.ws_col = 80;
	tty->winsize.ws_row = 25;
	tty->winsize.ws_xpixel = 0; /* TODO */
	tty->winsize.ws_ypixel = 0; /* TODO */
}

void tty_uninit(tty_t *tty) {
	cbuf_free(&tty->obuf);
	cbuf_free(&tty->ibuf);
	kfree(tty->canbuf);
	sync_destroy(&tty->lock);
}

void tty_destroy(tty_t *tty) {
	/*
	 * Wakeup everyone waiting for events on the
	 * tty.
	 */
	synchronized(&tty->lock) {
		F_SET(tty->flags, TTY_DEAD);
		tty_wakeup_bg(tty);
		tty_wakeup(tty, &tty->ibuf);
		tty_wakeup(tty, &tty->obuf);
	}

	/*
	 * Make sure no one can use the tty anymore
	 * via the devfs. This also wait's until
	 * there are no active file-ops running
	 * anymore and prevents further calls
	 * to the file-ops of the tty.
	 */
	tty_destroydev(tty);

	/*
	 * Drop the reference for the tty device file.
	 */
	tty_unref(tty);
}

static int tty_fop_open(file_t *file) {
	tty_t *tty = file_get_priv(file);
	return tty->driver->open(tty);
}

static void tty_fop_close(file_t *file) {
	tty_t *tty = file_get_priv(file);
	tty->driver->close(tty);
}

static ssize_t tty_fop_read(file_t *file, uio_t *uio) {
	tty_t *tty = file_get_priv(file);
	return tty->driver->read(tty, uio);
}

static ssize_t tty_fop_write(file_t *file, uio_t *uio) {
	tty_t *tty = file_get_priv(file);
	return tty->driver->write(tty, uio);
}

static ssize_t tty_fop_ioctl(file_t *file, int cmd, void *arg) {
	tty_t *tty = file_get_priv(file);
	return tty->driver->ioctl(tty, cmd, arg);
}

int tty_vmakedev(struct devfs_node *node, tty_t *tty, const char *fmt,
	va_list ap)
{
	tty_ref(tty);
	return vmakechar(node, MAJOR_TTY, 0600, &tty_fops, tty, &tty->dev, fmt,
		ap);
}

int tty_makedev(struct devfs_node *node, tty_t *tty, const char *fmt, ...) {
	va_list ap;
	int retv;

	va_start(ap, fmt);
	retv = tty_vmakedev(node, tty, fmt, ap);
	va_end(ap);

	return retv;
}

void tty_destroydev(tty_t *tty) {
	destroydev(tty->dev);
}

bool tty_is_ctty(tty_t *tty, proc_t *proc) {
	sync_assert(&tty->lock);
	sync_assert(&proc_list_lock);
	return tty->session == proc->pgrp->session->id;
}

void tty_signal(tty_t *tty, int sig) {
	sync_assert(&tty->lock);
	pgrp_kill(tty->fg_pgrp, sig, SIG_KERN);
}

int tty_wait_bg(tty_t *tty, int sig) {
	pid_t pgrp, fg;
	int err = 0;

	sync_assert(&tty->lock);
	if(sig == SIGTTOU && signal_is_ignored(sig)) {
		return 0;
	}

	while(err == 0 && (pgrp = kern_getpgrp()) != (fg = tty->fg_pgrp)) {
		pgrp_kill(pgrp, sig, SIG_KERN);
		sync_release(&tty->lock);
		err = kern_wait(&tty->fg_pgrp, fg, KWAIT_INTR);
		sync_acquire(&tty->lock);

		if(err == -EAGAIN) {
			err = 0;
		}
		if(err == 0 && F_ISSET(tty->flags, TTY_DEAD)) {
			err = -ENXIO;
		}
	}

	return err;
}

static int tty_sleep_buf(tty_t *tty, cbuf_t *cbuf, size_t size) {
	int err;

	assert(cbuf == &tty->ibuf || cbuf == &tty->obuf);
	sync_assert(&tty->lock);

	sync_release(&tty->lock);
	err = kern_wait(&cbuf->data, size, KWAIT_INTR);
	sync_acquire(&tty->lock);
	
	if(err == -EAGAIN) {
		err = 0;
	}
	if(err == 0 && F_ISSET(tty->flags, TTY_DEAD)) {
		err = -ENXIO;
	}

	return err;
}

int tty_wait_space(tty_t *tty, cbuf_t *cbuf) {
	if(cbuf_is_full(cbuf)) {
		return tty_sleep_buf(tty, cbuf,  cbuf_size(cbuf));
	} else {
		return 0;
	}
}

int tty_wait_data(tty_t *tty, cbuf_t *cbuf) {
	if(cbuf_is_empty(cbuf)) {
		return tty_sleep_buf(tty, cbuf, 0);
	} else {
		return 0;
	}
}

void tty_wakeup(tty_t *tty, cbuf_t *cbuf) {
	assert(cbuf == &tty->ibuf || cbuf == &tty->obuf);
	kern_wake(&cbuf->data, INT_MAX, 0);
}

void tty_wakeup_bg(tty_t *tty) {
	sync_assert(&tty->lock);
	kern_wake(&tty->fg_pgrp, INT_MAX, 0);
}

bool tty_output(tty_t *tty, char c) {
	sync_assert(&tty->lock);

	/*
	 * Don't do output processing if the corresponding
	 * flag is not set.
	 */
	if(!TTY_OISSET(tty, OPOST)) {
		return tty_oputc(tty, c);
	}

	/*
	 * Handle the newline and carriage return stuff.
	 */
	if(c == '\n' && TTY_OISSET(tty, ONLCR) && !tty_oputc(tty, '\r')) {
		return false;
	} else if(c == '\r') {
		if(TTY_OISSET(tty, OCRNL)) {
			c = '\n';
		} else if(TTY_OISSET(tty, ONOCR) && tty->column == 0) {
			return true;
		}
	}

	if(!tty_oputc(tty, c)) {
		return false;
	}

	switch(c) {
	case '\b':
		if(tty->column > 0) {
			tty->column--;
		}

		break;
	case '\n':
		if(TTY_OISSET(tty, ONLCR | ONLRET)) {
			tty->column = 0;
		}

		break;
	case '\t':
		tty->column = ALIGN(tty->column + 1, TTY_TABSIZE);
		break;
	case '\r':
		tty->column = 0;
		break;
	default:
		if(ASCII_CLASS(c) != ASCII_CONTROL) {
			tty->column++;
		}
		break;
	}

	return true;
}

void tty_echo(tty_t *tty, char c) {
	if((TTY_LISSET(tty, ECHO) ||
		(TTY_LISSET(tty, ECHONL) && c == '\n')) &&
		!TTY_LISSET(tty, EXTPROC))
	{
		tty_output(tty, c);
	}
}

static void tty_erase(tty_t *tty, size_t num) {
	while(num--) {
		tty_output(tty, '\b');
		tty_output(tty, ' ');
		tty_output(tty, '\b');
	}
}

bool tty_undo(tty_t *tty) {
	char c;

	assert(TTY_LISSET(tty, ICANON));
	if(tty->candata == 0) {
		return false;
	}

	tty->candata--;
	c = tty->canbuf[tty->candata];
	
	/*
	 * Visually erease character.
	 */
	if(TTY_LISSET(tty, ECHOE)) {
		switch(c) {
		case '\b':
		case '\n':
			if(TTY_LISSET(tty, ECHOCTL)) {
				tty_erase(tty, 2);
			}
			break;
		case '\t': {
			size_t tabsz, column, i;

			/*
			 * Erasing tabs is a little bit more difficult.
			 */
			column = tty->column;
			tty->column = tty->startcol;

			/*
			 * Recalculate tty->column based on the
			 * data in the canonical buffer without
			 * the last tab.
			 */
			F_SET(tty->flags, TTY_NOOUT);
			for(i = 0; i < tty->candata; i++) {
				tty_output(tty, tty->canbuf[i]);
			}
			F_CLR(tty->flags, TTY_NOOUT);

			tabsz = column - tty->column;
			tty->column = column;

			assert(tabsz <= TTY_TABSIZE);
			while(tabsz-- > 0) {
				tty_output(tty, '\b');
			}

			break;
		}
		default:
			tty_erase(tty, 1);
			break;
		}
	}

	return true;
}

static bool tty_input_c(tty_t *tty, char c) {
	if(TTY_LISSET(tty, ICANON)) {
		if(tty->candata < TTY_BUFSIZE) {
			tty->canbuf[tty->candata] = c;
			tty->candata++;
			return true;
		} else {
			return false;
		}
	} else {
		return cbuf_putc(&tty->ibuf, c);
	}
}

void tty_flush_output(tty_t *tty) {
	cbuf_discard(&tty->obuf);
}

void tty_flush_input(tty_t *tty) {
	cbuf_discard(&tty->ibuf);
	tty->candata = 0;
}

void tty_input(tty_t *tty, char c) {
	sync_assert(&tty->lock);

	if(!TTY_LISSET(tty, EXTPROC)) {
		if(TTY_LISSET(tty, ISIG)) {
			if(TTY_CCEQ(tty, VINTR, c)) {
				/*
				 * Handle CTRL-C.
				 */
				tty_flush_output(tty);
				tty_flush_input(tty);
				tty_echo(tty, '^');
				tty_echo(tty, 'C');
				tty_signal(tty, SIGINT);
				goto out;
			} else if(TTY_CCEQ(tty, VQUIT, c)) {
				/*
				 * Handle CTRL-\ aka quit.
				 */
				tty_flush_output(tty);
				tty_flush_input(tty);
				tty_echo(tty, '^');
				tty_echo(tty, '\\');
				tty_signal(tty, SIGQUIT);
				goto out;
			} else if(TTY_CCEQ(tty, VSUSP, c)) {
				/*
				 * Handle CTRL-Z aka suspend.
				 */
				tty_flush_input(tty);
				tty_echo(tty, '^');
				tty_echo(tty, 'Z');
				tty_signal(tty, SIGTSTP);
				goto out;
			}
		}

		if(TTY_LISSET(tty, ICANON)) {
			if(TTY_CCEQ(tty, VERASE, c)) {
				tty_undo(tty);
				goto out;
			} else if(TTY_CCEQ(tty, VKILL, c)) {
				/*
				 * Handle CTRL-U.
				 */
				if(TTY_LISSET(tty, ECHOKE)) {
					while(tty->candata) {
						tty_undo(tty);
					}
					goto out;
				} else {
					tty_echo(tty, c);
					tty->candata = 0;
				}
			}
		}
	}

	if(tty_input_c(tty, c)) {
		/*
		 * If a line is done in canonical mode, the data
		 * can be written to the input buffer.
		 */
		if(TTY_LISSET(tty, ICANON)) {
			if(tty_isbreak(tty, c)) {
				cbuf_write(&tty->ibuf, tty->candata,
					tty->canbuf);
				tty->candata = 0;
				tty_wakeup(tty, &tty->ibuf);
			} else if(tty->candata == 1) {
				tty->startcol = tty->column;
			}
		} else {
			tty_wakeup(tty, &tty->ibuf);
		}

		tty_echo(tty, c);
	} else {
		/*
		 * The line buffer is too small.
		 * TODO What to do here?
		 */
		tty_flush_input(tty);
	}

out:
	tty->driver->start(tty);
	return;
}

ssize_t tty_write(tty_t *tty, uio_t *uio) {
	size_t written = uio->size;
	char buf[128];
	int err;

	/*
	 * Wait while the current process is in the background.
	 */
	sync_acquire(&tty->lock);
	err = tty_wait_bg(tty, SIGTTOU);
	sync_release(&tty->lock);
	if(err) {
		return err;
	}

	while(uio->size) {
		char *ptr = buf;
		ssize_t res;
		size_t size;

		size = res = uiomove(ptr, sizeof(buf), uio);
		if(res < 0) {
			return res;
		}

		sync_acquire(&tty->lock);
		while(size > 0) {
			if(tty_output(tty, *ptr)) {
				size--;
				ptr++;
				continue;
			}

			/*
			 * The output buffer is full, so tell the device
			 * driver that data is avaliable to process and
			 * wait until there is some space.
			 */
			tty->driver->start(tty);
			err = tty_wait_space(tty, &tty->obuf);
			if(err) {
				sync_release(&tty->lock);

				/*
				 * The read/write syscalls expect a correct
				 * uio->size, especially if the thread was
				 * interrupted due to a signal. So let's
				 * adjust it, because the previous uiomove
				 * call may have read more data that
				 * we could process.
				 */
				uio->size += size;
				return err;
			}
		}

		/*
		 * Tell the tty driver about the new data.
		 */
		tty->driver->start(tty);
		sync_release(&tty->lock);
	}

	return written;
}

ssize_t tty_read(tty_t *tty, uio_t *uio) {
	ssize_t retv = 0;
	int err;
	char c;

	/*
	 * Wait while the current process is in the
	 * background.
	 */
	sync_acquire(&tty->lock);
	err = tty_wait_bg(tty, SIGTTIN);
	sync_release(&tty->lock);
	if(err) {
		return err;
	}

	sync_acquire(&tty->lock);
	err = tty_wait_data(tty, &tty->ibuf);
	if(err) {
		sync_release(&tty->lock);
		return err;
	}

	while(cbuf_getc(&tty->ibuf, &c)) {
		ssize_t res;

		if(TTY_CCEQ(tty, VEOF, c)) {
			break;
		}

		sync_release(&tty->lock);
		res = uiomove(&c, sizeof(c), uio);
		sync_acquire(&tty->lock);

		if(res < 0) {
			retv = res;
			break;
		}

		retv += res;
		if(uio->size == 0 || tty_isbreak(tty, c)) {
			break;
		}
	}

	sync_release(&tty->lock);
	return retv;
}

int tty_ioctl(tty_t *tty, int cmd, void *arg) {
	proc_t *proc = cur_proc();
	struct winsize winsize;
	struct termios term;
	pid_t pgrp = 0;
	int err;

	switch(cmd) {
	case TCGETS: {
		synchronized(&tty->lock) {
			term = tty->termios;
		}

		return copyout(arg, &term, sizeof(term));
	}
	case TCSETS:
	case TCSETSW:
	case TCSETSF: {
		size_t data;

		err = copyin(&term, arg, sizeof(term));
		if(err) {
			return err;
		}

		sync_scope_acquire(&tty->lock);
		if(cmd == TCSETSW || cmd == TCSETSF) {
			/*
			 * Wait for output to complete.
			 */
			while((data = cbuf_available(&tty->obuf)) != 0) {
				err =  tty_sleep_buf(tty, &tty->obuf, data);
				if(err) {
					return err;
				}
			}
		}

		if(cmd == TCSETSF) {
			tty_flush_input(tty);
		}

		tty->termios = term;
		return 0;
	}
	case TIOCGWINSZ: {
		synchronized(&tty->lock) {
			winsize = tty->winsize;
		}

		return copyout(arg, &winsize, sizeof(winsize));
	}
	case TIOCSWINSZ: {
		err = copyin(&winsize, arg, sizeof(winsize));
		if(!err) {
			return err;
		}

		sync_scope_acquire(&tty->lock);
		tty->winsize = winsize;
		tty_signal(tty, SIGWINCH);

		return 0;
	}
	case TIOCSCTTY:
		return session_set_tty(tty);
	case TIOCGPGRP: {
		synchronized(&tty->lock) {
			pgrp = tty->fg_pgrp;
		}

		return copyout(arg, &pgrp, sizeof(pgrp));
	}
	case TIOCSPGRP: {
		err = copyin(&pgrp, arg, sizeof(pgrp));
		if(err) {
			return err;
		} else if(pgrp <= 0) {
			return -EINVAL;
		}

		sync_scope_acquire(&proc_list_lock);
		if(!pgrp_present(pgrp, proc->pgrp->session->id)) {
			return -EPERM;
		}

		synchronized(&tty->lock) {
			if(!tty_is_ctty(tty, proc)) {
				return -ENOTTY;
			}

			tty->fg_pgrp = pgrp;
			tty_wakeup_bg(tty);
		}

		/*
		 * TODO
		 * "If tcsetpgrp() is called by a member of a background
		 * process group in its session, and the calling process
		 * is not blocking or ignoring SIGTTOU, a SIGTTOU signal
		 * is sent to all members of this background process group"
		 */
		return 0;
	}
	case TIOCGLCKTRMIOS:
	case TIOCSLCKTRMIOS:
	case TCSBRK:
	case TCSBRKP:
	case TIOCSBRK:
	case TIOCCBRK:
	case TCXONC:
	case TIOCGSID:
	case TIOCNOTTY:
	default:
		/*
		 * TODO
		 */
		kprintf("[tty] todo: 0x%x\n", cmd);
		return -EINVAL;
	}
}

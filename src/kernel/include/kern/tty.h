#ifndef KERN_TTY_H
#define KERN_TTY_H

#include <kern/atomic.h>
#include <kern/sync.h>
#include <sys/termios.h>
#include <sys/ioctl.h>
#include <lib/cbuf.h>

struct tty;
struct uio;
struct devfs_node;

#define TTY_OISSET(tty, f)	F_ISSET((tty)->termios.c_oflag, f)
#define TTY_LISSET(tty, f)	F_ISSET((tty)->termios.c_lflag, f)
#define TTY_CCEQ(tty, c, x)	((tty)->termios.c_cc[(c)] == (x))

#define TTY_TABSIZE		8
#define TTY_BUFSIZE		1024

typedef int tty_open_t (struct tty *);
typedef void tty_close_t (struct tty *);
typedef ssize_t tty_read_t (struct tty *, struct uio *);
typedef ssize_t tty_write_t (struct tty *, struct uio *);
typedef int tty_ioctl_t (struct tty *, int, void *);
typedef void tty_free_t (struct tty *);
typedef void tty_start_t (struct tty *);

typedef struct tty_driver {
	tty_open_t *open;
	tty_close_t *close;
	tty_read_t *read;
	tty_write_t *write;
	tty_ioctl_t *ioctl;
	tty_free_t *free;
	tty_start_t *start;
} tty_driver_t;

/**
 * @brief A tty line discipline.
 */
typedef struct tty_ldisc {
	const char *name;

	ssize_t (*read)	(struct tty *, struct uio *);
	ssize_t (*write) (struct tty *, struct uio *);
	void (*input) (struct tty *, char c);
} tty_ldisc_t;

typedef struct tty {
	sync_t lock;

	pid_t session;
	pid_t fg_pgrp;

	tty_ldisc_t *ldisc;

	cbuf_t obuf; /* output buffer */
	cbuf_t ibuf; /* input buffer */
	char *canbuf; /* canonical buffer */
	size_t candata;
	size_t startcol; /* column of the start of the line */
	size_t column;

#define TTY_NOOUT	(1 << 0)
#define TTY_DEAD	(1 << 1)
	int flags;
	struct winsize winsize;
	struct termios termios;

	void *priv;
	tty_driver_t *driver;

	ref_t ref;
	dev_t dev;
} tty_t;

tty_t *tty_ref(tty_t *tty);
void tty_unref(tty_t *tty);

int tty_vmakedev(struct devfs_node *node, tty_t *tty, const char *fmt,
	va_list ap);
int tty_makedev(struct devfs_node *node, tty_t *tty, const char *fmt, ...);
void tty_destroydev(tty_t *tty);

void tty_init(tty_t *tty, tty_driver_t *driver, void *priv);
void tty_uninit(tty_t *tty);
void tty_destroy(tty_t *tty);

void tty_signal(tty_t *tty, int sig);
int tty_wait_bg(tty_t *tty, int sig);
void tty_wakeup_bg(tty_t *tty);
void tty_default(tty_t *tty);

static inline bool tty_isbreak(tty_t *tty, char c) {
	return c == '\n' || TTY_CCEQ(tty, VEOF, c) || TTY_CCEQ(tty, VEOL, c);
}

static inline bool tty_oputc(tty_t *tty, char c) {
	if(F_ISSET(tty->flags, TTY_NOOUT)) {
		return true;
	} else {
		return cbuf_putc(&tty->obuf, c);
	}
}

int tty_wait_data(tty_t *tty, cbuf_t *cbuf);
int tty_wait_space(tty_t *tty, cbuf_t *cbuf);
void tty_wakeup(tty_t *tty, cbuf_t *cbuf);

void tty_flush_output(tty_t *tty);
void tty_flush_input(tty_t *tty);

bool tty_output(tty_t *tty, char c);
void tty_echo(tty_t *tty, char c);
void tty_input(tty_t *tty, char c);
int tty_ioctl(tty_t *tty, int cmd, void *arg);
ssize_t tty_write(tty_t *tty, struct uio *uio);
ssize_t tty_read(tty_t *tty, struct uio *uio);

static inline ssize_t tty_ldisc_write(tty_t *tty, struct uio *uio) {
	return tty->ldisc->write(tty, uio);
}

static inline ssize_t tty_ldisc_read(tty_t *tty, struct uio *uio) {
	return tty->ldisc->read(tty, uio);
}

#endif
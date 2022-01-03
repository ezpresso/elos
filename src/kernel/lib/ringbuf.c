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
#include <kern/atomic.h>
#include <kern/futex.h>
#include <vfs/uio.h>
#include <lib/ringbuf.h>
#include <sys/limits.h>

int ringbuf_alloc(ringbuf_t *rb, size_t sz) {
	cbuf_alloc(&rb->buf, sz, VM_WAIT);
	sync_init(&rb->lock, SYNC_MUTEX);
	rb->eof = false;
	return 0;
}

void ringbuf_free(ringbuf_t *rb) {
	sync_destroy(&rb->lock);
	cbuf_free(&rb->buf);
}

void ringbuf_eof(ringbuf_t *rb) {
	sync_acquire(&rb->lock);
	rb->eof = true;
	sync_release(&rb->lock);
	kern_wake(&rb->buf.data, INT_MAX, 0);
}

static int ringbuf_sleep(ringbuf_t *rb, size_t size) {
	int err;

	sync_assert(&rb->lock);
	sync_release(&rb->lock);
	err = kern_wait(&rb->buf.data, size, KWAIT_INTR);
	sync_acquire(&rb->lock);

	return err;
}

static int ringbuf_rdwait(ringbuf_t *rb, int flags) {
	int err;

	sync_assert(&rb->lock);
	while(cbuf_is_empty(&rb->buf)) {
		if(rb->eof) {
			return -EPIPE;
		} else if(F_ISSET(flags, RB_NOBLOCK)) {
			return -EWOULDBLOCK;
		}

		err = ringbuf_sleep(rb, 0);
		if(err) {
			return err;
		}
	}

	return 0;
}

ssize_t ringbuf_read(ringbuf_t *rb, size_t size, void *buf, int flags) {
	ssize_t retv;

	sync_acquire(&rb->lock);
	retv = ringbuf_rdwait(rb, flags);
	if(retv == 0) {
		retv = cbuf_read(&rb->buf, size, buf);
		kern_wake(&rb->buf.data, INT_MAX, 0);
	} else if(retv == -EPIPE) {
		retv = 0;
	}

	sync_release(&rb->lock);
	return retv;
}

ssize_t ringbuf_write(ringbuf_t *rb, size_t size, void *buf, int flags) {
	ssize_t retv;

	sync_acquire(&rb->lock);
	assert(!rb->eof);

	while(cbuf_is_full(&rb->buf)) {
		if(F_ISSET(flags, RB_NOBLOCK)) {
			retv = 0;
			goto out;
		}

		retv = ringbuf_sleep(rb, cbuf_size(&rb->buf));
		if(retv) {
			goto out;
		}
	}

	retv = cbuf_write(&rb->buf, size, buf);
	kern_wake(&rb->buf.data, INT_MAX, 0);
out:
	sync_release(&rb->lock);
	return retv;
}

ssize_t ringbuf_read_uio(ringbuf_t *rb, uio_t *uio, int flags) {
	size_t prev = uio->size, size;
	char buf[128];
	int err;

	/*
	 * Read as much data as we can read with one wait.
	 */
	sync_acquire(&rb->lock);
	err = ringbuf_rdwait(rb, flags);
	if(err) {
		sync_release(&rb->lock);
		return err;
	}

	while(uio->size && (size = cbuf_read(&rb->buf,
		min(uio->size, sizeof(buf)), buf)) > 0)
	{
		sync_release(&rb->lock);

		err = uiomove(buf, size, uio);
		if(err < 0) {
			return err;
		}

		sync_acquire(&rb->lock);
	}

	sync_release(&rb->lock);
	return prev - uio->size;
}

ssize_t ringbuf_write_uio(ringbuf_t *rb, uio_t *uio, int flags) {
	size_t prev = uio->size;
	char buf[128];

	while(uio->size) {
		ssize_t retv;
		size_t moved;

		moved = retv = uiomove(buf, sizeof(buf), uio);
		if(retv < 0) {
			return retv;
		}

		while(moved) {
			retv = ringbuf_write(rb, retv, buf, flags);
			if(retv < 0) {
				uio->size += moved;
				return retv;
			} else if(retv == 0 && F_ISSET(flags, RB_NOBLOCK)) {
				uio->size += moved;
				break;
			} else {
				moved -= retv;
			}
		}
	}

	return prev - uio->size;
}

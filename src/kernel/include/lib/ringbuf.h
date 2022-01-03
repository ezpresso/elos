#ifndef LIB_RINGBUF_H
#define LIB_RINGBUF_H

#include <kern/sync.h>
#include <lib/cbuf.h>

#define RB_NOBLOCK (1 << 0)

struct uio;

typedef struct ringbuf {
	cbuf_t buf;
	sync_t lock;
	bool eof;
} ringbuf_t;

/**
 * @brief Allocate the necessery memory for the ringbuffer.
 */
int ringbuf_alloc(ringbuf_t * rb, size_t sz);


/**
 * @brief Free the buffer associated with the ringbuf structure.
 */
void ringbuf_free(ringbuf_t * rb);

/*
 * @brief Write data into the ringbuffer.
 *
 * @return	-EINTR 	if thread was interrupted and @p noblock was false
 *		-EPIPE	if ringbuf_eof was called while waiting for free-space
 * 		-EWOULDBLOCK no space available and the RB_NOBLOCK flag was set
 *		else 	number of bytes written to the buffer	
 */
ssize_t ringbuf_write(ringbuf_t *rb, size_t size, void *buf, int flags);
ssize_t ringbuf_write_uio(ringbuf_t *rb, struct uio *uio, int flags);

/**
 * @brief Read data from the ringbuffer.
 *
 * @return	-ERESTART
 *		-EINTR 	if thread was interrupted and the RB_NOBLOCK flag was
 *			not set was false
 *		0 	if ringbuf_eof was called and no data is left in the
 *			buffer or the RB_NOBLOCK flag was set and there was
 *			nothing to be read
 *		> 0	number of bytes read on success
 */
ssize_t ringbuf_read(ringbuf_t *rb, size_t size, void *buf, int flags);
ssize_t ringbuf_read_uio(ringbuf_t *rb, struct uio *uio, int flags);

/** 
 * @brief Don't allow any further writes.
 */
void ringbuf_eof(ringbuf_t *rb);

#endif
#ifndef LIB_CBUF_H
#define LIB_CBUF_H

#include <vm/flags.h>

typedef struct cbuf {
	void *first;
	void *last;
	void *rptr;
	void *wptr;
	size_t data;
} cbuf_t;

static inline size_t cbuf_size(cbuf_t *cbuf) {
	return cbuf->last - cbuf->first;
}

static inline bool cbuf_is_full(cbuf_t *cbuf) {
	return cbuf->data == cbuf_size(cbuf);
}

static inline bool cbuf_is_empty(cbuf_t *cbuf) {
	return cbuf->data ==  0;
}

static inline size_t cbuf_available(cbuf_t *cbuf) {
	return cbuf->data;
}

int cbuf_alloc(cbuf_t *buf, size_t size, vm_flags_t flags);
void cbuf_free(cbuf_t *buf);
size_t cbuf_read(cbuf_t *cbuf, size_t size, void *buf);
size_t cbuf_write(cbuf_t *cbuf, size_t size, void *buf);

static inline bool cbuf_getc(cbuf_t *cbuf, char *c) {
	return cbuf_read(cbuf, 1, c) == 1;
}

static inline bool cbuf_putc(cbuf_t *cbuf, char c) {
	return cbuf_write(cbuf, 1, &c) == 1;
}

static inline void cbuf_discard(cbuf_t *cbuf) {
	cbuf->rptr = cbuf->first;
	cbuf->wptr = cbuf->first;
	cbuf->data = 0;
}

#endif
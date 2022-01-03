#ifndef NET_PACKET_H
#define NET_PACKET_H

#include <kern/heap.h>

typedef struct packet {
	void *alloc;
	void *data;
	void *tail;

	size_t total_sz;
	size_t length;
} packet_t;

static inline int packet_alloc(packet_t *packet, size_t hdrsz, size_t bufsz) {
	packet->alloc = kvalloc(hdrsz + bufsz);
	if(!packet->alloc) {
		return -ENOMEM;
	}

	packet->data = packet->alloc + hdrsz;
	packet->tail = packet->data;
	packet->total_sz = hdrsz + bufsz;
	packet->length = 0;

	return 0;
}

static inline void packet_init(packet_t *packet, void *ptr, size_t sz) {
	packet->alloc = NULL;

	packet->data = ptr;
	packet->tail = ptr + sz;
	packet->total_sz = sz;
	packet->length = sz;
}

static inline void *packet_add_hdr(packet_t *packet, size_t sz) {
	kassert((size_t)(packet->data - packet->alloc) > sz);

	packet->data -= sz;
	packet->length += sz;

	return packet->data;
}

static inline void *packet_get_ptr(packet_t *packet) {
	return packet->data;
}

static inline void *packet_pull(packet_t *packet, size_t sz) {
	void *tmp = packet->data;
	packet->data += sz;
	return tmp;
}

static inline uint8_t packet_pull_byte(packet_t *packet) {
	uint8_t *tmp;
	tmp = packet_pull(packet, 1);
	return *tmp;
}

static inline uint32_t packet_pull_dword(packet_t *packet) {
	uint32_t *tmp;
	tmp = packet_pull(packet, 4);
	return *tmp;
}

static inline void *packet_add_data(packet_t *packet, size_t sz) {
	void *ptr;

	kassert(sz < packet->total_sz - (packet->tail - packet->alloc));

	ptr = packet->tail;
	packet->tail += sz;
	packet->length += sz;

	return ptr;
}

static inline void packet_add_byte(packet_t *packet, uint8_t val) {
	uint8_t *data;

	data = packet_add_data(packet, 1);
	*data = val;
}

static inline void packet_add_dword(packet_t *packet, uint32_t val) {
	uint32_t *data;

	data = packet_add_data(packet, 4);
	*data = val;
}

#endif
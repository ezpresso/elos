#ifndef LIB_CHECKSUM_H
#define LIB_CHECKSUM_H

static inline uint8_t checksum(void * addr, int l) {
	uint8_t *a = addr;
	uint8_t sum = 0;

	for(int i = 0; i < l; i++) {
		sum += a[i];
	}

	return sum;
}

#endif
#ifndef NET_CHECKSUM_H
#define NET_CHECKSUM_H

static inline uint32_t net_checksum_add(void *ptr, int len) {
	uint8_t *buf = ptr;
    uint32_t sum = 0;
    int i;

	for(i = 0; i < len; i++) {
		if(i & 1) {
		    sum += (uint32_t)buf[i];
		} else {
	    	sum += (uint32_t)buf[i] << 8;
		}
    }

	return sum;
}

static inline uint16_t net_checksum_finish(uint32_t sum) {
	while(sum >> 16) {
		sum = (sum & 0xFFFF) + (sum >> 16);
	}

	return ~sum;
}

static inline uint16_t net_checksum(void *ptr, int len) {
	return net_checksum_finish(net_checksum_add(ptr, len));
}

#endif
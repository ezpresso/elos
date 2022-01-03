#ifndef LIB_BIN_H
#define LIB_BIN_H

static inline uint8_t bcd2bin(uint8_t val) {
	return ((val & 0xF0) >> 4) * 10 + (val & 0x0F);
}

#endif
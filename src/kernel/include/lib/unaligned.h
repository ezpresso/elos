#ifndef LIB_UNALIGNED_H
#define LIB_UNALIGNED_H

struct __unaligned_u16 {
	uint16_t value;
} __packed;

struct __unaligned_u32 {
	uint32_t value;
} __packed;

struct __unaligned_u64 {
	uint64_t value;
} __packed;

static inline uint16_t unaligned_read16(const void *ptr) {
	return ((const struct __unaligned_u16 *) ptr)->value;
}

static inline uint32_t unaligned_read32(const void *ptr) {
	return ((const struct __unaligned_u32 *) ptr)->value;
}

static inline uint64_t unaligned_read64(const void *ptr) {
	return ((const struct __unaligned_u64 *) ptr)->value;
}

static inline void unaligned_write16(void *ptr, uint16_t value) {
	((struct __unaligned_u16 *) ptr)->value = value;
}

static inline void unaligned_write32(void *ptr, uint32_t value) {
	((struct __unaligned_u32 *) ptr)->value = value;
}

static inline void unaligned_write64(void *ptr, uint64_t value) {
	((struct __unaligned_u64 *) ptr)->value = value;
}

#endif
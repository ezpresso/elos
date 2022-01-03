#ifndef KERN_ENDIAN_H
#define KERN_ENDIAN_H

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Wrong endian
#endif

typedef uint32_t be32_t;
typedef uint16_t be16_t;

#define be32_to_cpu(x) __builtin_bswap32((uint32_t)(x))
#define be16_to_cpu(x) __builtin_bswap16((uint16_t)(x))
#define cpu_to_be32(x) __builtin_bswap32((uint32_t)(x))
#define cpu_to_be16(x) __builtin_bswap16((uint16_t)(x))

#endif

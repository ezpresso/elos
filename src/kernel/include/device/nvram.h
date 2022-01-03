#ifndef DEVICE_NVRAM_H
#define DEVICE_NVRAM_H

#include <lib/list.h>

typedef struct nvram {
	struct nvram_driver *driver;
	list_node_t node;
	const char *name;
} nvram_t;

typedef struct nvram_driver {
	int (*nvram_rw) (nvram_t *, bool wr, uint64_t off, uint8_t size,
		uint64_t *value);
} nvram_driver_t;

/**
 * @brief Search an nvram device with a given name
 *	TODO: Locks a mutex until nvram_put because
 *	device reference counting is not implemented
 *	yet
 */
nvram_t *nvram_search(const char *name);

/**
 * @brief @see nvram_search
 */
void nvram_put(nvram_t *nvram);

/**
 * @brief nvram accesss
 */
#define nvram_readb(n, o, v) nvram_read(n, o, 1, v)
#define nvram_readw(n, o, v) nvram_read(n, o, 2, v)
#define nvram_readl(n, o, v) nvram_read(n, o, 4, v)
#define nvram_readq(n, o, v) nvram_read(n, o, 8, v)
#define nvram_writeb(n, o, v) nvram_write(n, o, 1, v)
#define nvram_writew(n, o, v) nvram_write(n, o, 2, v)
#define nvram_writel(n, o, v) nvram_write(n, o, 4, v)
#define nvram_writeq(n, o, v) nvram_write(n, o, 8, v)
int nvram_read(nvram_t *nvram, uint64_t off, uint8_t size, uint64_t *value);
int nvram_write(nvram_t *nvram, uint64_t off, uint8_t size, uint64_t value);

/**
 * @brief Register an nvram driver
 */
int nvram_register(nvram_t *nvram, nvram_driver_t *driver, const char *name);

/**
 * @brief Unregister an nvram driver
 */
void nvram_unregister(nvram_t *nvram);

#endif
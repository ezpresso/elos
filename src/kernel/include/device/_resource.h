#ifndef DEVICE_RESOURCE_H
#define DEVICE_RESOURCE_H

#include <lib/resman.h>

struct bus_res;

/**
 * @brief Check whether a bus address is supported or not.
 */
#define BUS_ADDR_SUPP(addr, size) \
	((addr) < BUS_ADDR_MAX && BUS_ADDR_MAX - (addr) < (size))

/**
 * @brief Check whether a resource resides at a fixed address.
 */
#define BUS_ADDR_FIXED(addr, end, size) \
	((addr) + (size) - 1 == (end))

/**
 * @brief Check whether a resource resides at a fixed address.
 */
#define BUS_ADDR_ANY(addr, end, size) \
	((addr) == 0 && (end) == BUS_ADDR_MAX && (size) == BUS_ADDR_MAX)

#define BUS_ADDR_ANYWHERE(addr, end, size) \
	((addr) == 0 && (end) == BUS_ADDR_MAX && (size) != BUS_ADDR_MAX)

#define BUS_IS_TRIG_LEVEL(x)	(((x) & BUS_INTR_TRIG_MASK) == BUS_TRIG_LVL)
#define BUS_IS_TRIG_EDGE(x)	(((x) & BUS_INTR_TRIG_MASK) == BUS_TRIG_EDGE)
#define BUS_IS_POL_HIGH(x)	(((x) & BUS_INTR_POL_MASK) == BUS_POL_HI)
#define BUS_IS_POL_LOW(x)	(((x) & BUS_INTR_POL_MASK) == BUS_POL_LO)
#define BUS_INTR_TRIG_MASK	(1 << 0)
#define 	BUS_TRIG_LVL		(0 << 0)
#define 	BUS_TRIG_EDGE		(1 << 0)
#define BUS_INTR_POL_MASK	(1 << 1)
#define 	BUS_POL_HI		(0 << 1)
#define 	BUS_POL_LO		(1 << 1)
#define BUS_INTR_SHARED		(1 << 2)
/**
 * For internal use only (concerning interrupt controllers). Drivers must
 * not set this flag.
 */
#define BUS_INTR_MASKED		(1 << 3)

/*
 * Flags for bus_alloc_res family
 */
#define RF_NONE	0
#define RF_MAP (1 << 0)

/**
 * @brief Return values for interrupt handler.
 */
enum {
	BUS_INTR_STRAY,
	BUS_INTR_OK, /* Handled the interrupt */
	BUS_INTR_ITHR, /* Schedule interrupt thread */
};

typedef int  (*bus_intr_hand_t)		(void *arg, int intr);
typedef void (*bus_intr_thand_t)	(void *arg);

typedef struct bus_res_acc {
	uint8_t  (*readb) (struct bus_res *, bus_off_t);
	uint16_t (*readw) (struct bus_res *, bus_off_t);
	uint32_t (*readl) (struct bus_res *, bus_off_t);
	uint64_t (*readq) (struct bus_res *, bus_off_t);
	void (*writeb) (struct bus_res *, bus_off_t, uint8_t);
	void (*writew) (struct bus_res *, bus_off_t, uint16_t);
	void (*writel) (struct bus_res *, bus_off_t, uint32_t);
	void (*writeq) (struct bus_res *, bus_off_t, uint64_t);
} bus_res_acc_t;

typedef enum bus_res_type {
	BUS_RES_MEM = 0,
	BUS_RES_IO,
	BUS_RES_INTR,
	/* BUS_RES_BUS, PCI bus numbers */
	BUS_RES_MAX
} bus_res_type_t;

/**
 * @brief A bus resource allocated using the bus_alloc_res family.
 */
typedef struct bus_res {
	device_t *device;
	bus_res_type_t type;
	resman_t res;
	bool setup;

	/**
	 * @brief List of bus private data associated with the resource.
	 *
	 * Every bus involved with this resrouce may add private data
	 * to a resource.
	 */
	struct bus_res_priv *priv;

	union {
		struct {
			/*
			 * NULL if resource is not mapped.
			 */
			bus_res_acc_t *acc;
			device_t *map_dev;
			void *map;
		} map;

		struct {
			list_node_t node;
			bus_intr_hand_t hand;
			bus_intr_thand_t thand;
			void *arg;
			int flags;
		} intr;
	};
} bus_res_t;

typedef struct bus_res_req {
	bus_res_type_t type;
	bus_addr_t addr;
	bus_addr_t end;
	bus_size_t size;
	bus_size_t align;
	int bus_id;
	bus_res_t *res;
} bus_res_req_t;

const char *bus_res_type_name(bus_res_type_t type);

/**
 * @brief Allocate a res of a specific type in a specific range
 *
 * @param device 	the device which will be using the resource
 * @param type 		the type of the resource (e.g. BUS_RES_MEM)
 * @param bus_id 	a bus specific value (e.g. PCI BAR index).
 *			If the resource is not bus specific resource, bus_id
 *			has to be zero.
 * @param start 	the start of the range where the resource can be located
 * @param end 		the end of the range where the resource can be located
 *			end for the range addr,size: addr + size - 1
 * @param size 		the size of the resource
 * @param align
 * @param res 		a pointer to a structure which describes the resource
 * @return 		0 on success
 *			negative value on error
 */
int bus_alloc_res_at(device_t *device, bus_res_type_t type, int bus_id,
	bus_addr_t start, bus_addr_t end, bus_size_t size, bus_size_t align,
	int flags, bus_res_t *res);

static inline int bus_alloc_res_anywhere(device_t *device, bus_res_type_t type,
	int bus_id, bus_size_t size, bus_size_t align, int flags,
	bus_res_t *res)
{
	return bus_alloc_res_at(device, type, bus_id, 0, BUS_ADDR_MAX, size,
		align, flags, res);
}

/**
 * @brief Allocate a resource at a fixed location!
 * @see bus_alloc_res_at
 */
int bus_alloc_res_fixed(device_t *device, bus_res_type_t type, int bus_id,
	bus_addr_t start, bus_size_t size, int flags, bus_res_t *res);

/**
 * @brief Allocate a resource of a specific type.
 * @see bus_alloc_res_at
 */
int bus_alloc_res(device_t *, bus_res_type_t type, int bus_id, int flags,
	bus_res_t *res);

/**
 * @brief Allocate a resource of a specific type
 * @see bus_alloc_res
 * @param name		the name of the resource (used to distinguish multiple
 *			resources of the same type).
 */
int bus_alloc_res_name(device_t *, bus_res_type_t type, int bus_id,
	const char *name, int flags, bus_res_t *res);

/**
 * @brief	Free a resource allocate through _bus_alloc_res_ - family
 *			If the resource was mapped using bus_map_res, it will
 *			be unmapped automatically!
 * @param device 	the device which allocated the resource
 * @param res 		the resource that has previously been allocated
 */
void bus_free_res(device_t *device, bus_res_t *res);

/**
 * Map an IO/mem resource, so that it can be written to or read from using the
 * bus_res_readX and bus_res_writeX functions
 */
int bus_map_res(device_t *device, bus_res_t *res);

/**
 * @brief Unmap an IO/mem resource mapped using bus_map_res
 * @see bus_free_res
 */
void bus_unmap_res(device_t *device, bus_res_t *res);

/**
 * @brief Register handler and enable interrupt.
 */
int bus_setup_intr(device_t *device, bus_intr_hand_t hand,
	bus_intr_thand_t thand, void *arg, bus_res_t *res);

/**
 * @brief Undo bus_setup_intr
 */
void bus_teardown_intr(device_t *device, bus_res_t *res);

/**
 * @brief Update an resource allocation request based on device properties.
 *
 * Looks up the appropriate device property and updates the request
 * accordingly if the resource is a "default" resource (i.e. the allocating
 * device did not set addr, end, size when allocating).
 */
void bus_adjust_resource(device_t *device, const char *name,
	bus_res_req_t *req);

/**
 * @brief Initialize a resource.
 *
 * When a bus driver does not allocate a resrouce using the resman
 * interface, it can initialize the address and size of a resource
 * using this function.
 */
static inline void bus_res_init(bus_res_t *res, bus_addr_t addr,
	bus_size_t size)
{
	resman_init_root(&res->res, addr, (resman_addr_t)addr +
		(resman_size_t)size - 1);
}

/**
 * @brief Undo bus_res_init
 */
static inline void bus_res_destroy(bus_res_t *res) {
	resman_destroy_root(&res->res);
}

/**
 * @brief Add a way to access a specific resource.
 */
static inline void bus_res_set_acc(device_t *bus, bus_res_t *res,
	bus_res_acc_t *acc)
{
	res->map.acc = acc;
	res->map.map_dev = bus;
}

/**
 * @brief Allocate bus private data for a child resource.
 *
 * Allocate bus private data for a child resource, which can be looked up
 * later on using bus_res_get_priv. The memory allocated here is automatically
 * freed when the resource is being freed.
 *
 * @brief bus The device that wants to store information inside a resource.
 * @brief res The resource
 * @brief size The size of the private data
 * @retval A pointer to the private data.
 *
 */
void *bus_res_add_priv(device_t *bus, bus_res_t *res, size_t size);

/**
 * @brief Get the resource private data of an bus of a resource.
 */
void *bus_res_get_priv(device_t *bus, bus_res_t *res);

static inline bus_addr_t bus_res_get_addr(bus_res_t *res) {
	return resman_get_addr(&res->res);
}

static inline bus_addr_t bus_res_get_size(bus_res_t *res) {
	return resman_get_size(&res->res);
}

static inline bus_addr_t bus_res_get_end(bus_res_t *res) {
	return resman_get_end(&res->res);
}

static inline device_t *bus_res_get_device(bus_res_t *res) {
	return res->device;
}

static inline int bus_call_intr_handler(bus_res_t *res, int intr) {
	kassert(res->type == BUS_RES_INTR, "[bus] expected interrupt resource: "
		"%d", res->type);
	return res->intr.hand(res->intr.arg, intr);
}

#define __bus_res_read(res, off, size) ({ \
	assert((res)->map.acc && (res)->map.acc->read ## size); \
	(res)->map.acc->read ## size (res, off); \
})

#define __bus_res_write(res, off, val, size) ({ \
	assert((res)->map.acc && (res)->map.acc->write ## size ); \
	(res)->map.acc->write ## size (res, off, val); \
})

#define __bus_res_mask(res, off, clear, set, size, width) ({ \
	uint ## width ## _t tmp = bus_res_read ## size (res, off); \
	bus_res_write ## size (res, off, (tmp & ~clear) | set); \
	tmp; \
})

#define bus_res_read8(res, off) bus_res_readb(res, off)
#define bus_res_read16(res, off) bus_res_readw(res, off)
#define bus_res_read32(res, off) bus_res_readl(res, off)
#define bus_res_read64(res, off) bus_res_readq(res, off)
#define bus_res_write8(res, off, val) bus_res_writeb(res, off, val)
#define bus_res_write16(res, off, val) bus_res_writew(res, off, val)
#define bus_res_write32(res, off, val) bus_res_writel(res, off, val)
#define bus_res_write64(res, off, val) bus_res_writeq(res, off, val)

static inline uint8_t bus_res_readb(bus_res_t *res, bus_off_t off) {
	return __bus_res_read(res, off, b);
}

static inline uint16_t bus_res_readw(bus_res_t *res, bus_off_t off) {
	return __bus_res_read(res, off, w);
}

static inline uint32_t bus_res_readl(bus_res_t *res, bus_off_t off) {
	return __bus_res_read(res, off, l);
}

static inline uint64_t bus_res_readq(bus_res_t *res, bus_off_t off) {
	return __bus_res_read(res, off, q);
}

static inline void bus_res_writeb(bus_res_t *res, bus_off_t off, uint8_t val) {
	__bus_res_write(res, off, val, b);
}

static inline void bus_res_writew(bus_res_t *res, bus_off_t off, uint16_t val) {
	__bus_res_write(res, off, val, w);
}

static inline void bus_res_writel(bus_res_t *res, bus_off_t off, uint32_t val) {
	__bus_res_write(res, off, val, l);
}

static inline void bus_res_writeq(bus_res_t *res, bus_off_t off, uint64_t val) {
	__bus_res_write(res, off, val, q);
}

static inline uint8_t bus_res_maskb(bus_res_t *res, bus_off_t off,
	uint8_t clear, uint8_t set)
{
	return __bus_res_mask(res, off, clear, set, b, 8);
}

static inline uint16_t bus_res_maskw(bus_res_t *res, bus_off_t off,
	uint16_t clear, uint16_t set)
{
	return __bus_res_mask(res, off, clear, set, w, 16);
}

static inline uint32_t bus_res_maskl(bus_res_t *res, bus_off_t off,
	uint32_t clear, uint32_t set)
{
	return __bus_res_mask(res, off, clear, set, l, 32);
}

static inline uint64_t bus_res_maskq(bus_res_t *res, bus_off_t off,
	uint64_t clear, uint64_t set)
{
	return __bus_res_mask(res, off, clear, set, q, 64);
}

#endif

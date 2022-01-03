#ifndef DEVICE_DEVICE_H
#define DEVICE_DEVICE_H

#include <kern/sync.h>
#include <kern/driver.h>
#include <arch/bus.h>
#include <lib/list.h>

#define bus_foreach_child(device, bus)				\
	for((device) = bus_child_next(bus, NULL);		\
		(device) != NULL;				\
		(device) = bus_child_next(bus, (device)))

/*
 * Device flags.
 */
#define DEVICE_PERM		(1 << 0) /* Device cannot be freed */
#define DEVICE_INTR_ROOT	(1 << 1)

/**
 * @brief The section of the kernel binary where device tables are located.
 */
#define DEVTAB_SEC section(devtab, devtab_t)

/**
 * @brief Define a devide driver.
 */
#define DRIVER(name)				\
	/* static */ device_driver_t name;	\
	driver(&name, DRIVER_DEVICE);		\
	static device_driver_t name =

/**
 * @brief Define a device id table.
 */
#define DEVTAB(name, drv, idtype) 				\
	/* static */ devtab_entry_t name ## _entries[];		\
	static section_entry(DEVTAB_SEC) devtab_t name = { 	\
		.driver = drv, 					\
		.type = idtype, 				\
		.entries = name ## _entries			\
	};							\
	static devtab_entry_t name ## _entries[] =

/**
 * @brief The end element of a device id table.
 */
#define DEVTAB_END .id = DEVTAB_END_ID

typedef struct devtab_entry {
#define DEVTAB_END_ID 0
	uint64_t id;
	void *priv;
} devtab_entry_t;

typedef struct devtab {
	list_node_t node;
	const char *driver;
	const char *type;
	devtab_entry_t *entries;
} devtab_t;

/**
 * @brief Return values for the driver probe callback.
 */
enum {
	DEVICE_PROBE_FAIL = 0,
	DEVICE_PROBE_OK,
	DEVICE_PROBE_LOPRIO,
};

/**
 * @brief The type of a device.
 */
typedef enum device_type {
	/*
	 * Devices loaded early except if DEV_NO_EARLY flag is set.
	 */
	DEVICE_ROOT,
	DEVICE_BUS,
	DEVICE_CHIPSET,
	DEVICE_INTR_CNTLR,
	DEVICE_UART,
	DEVICE_CLOCK,
	DEVICE_TIMER,

	/*
	 * Devices not loaded early.
	 */
	DEVICE_INPUT,
	DEVICE_NET,
	DEVICE_DISK,
	DEVICE_GPU,
} device_type_t;

typedef struct device device_t;

typedef struct device_driver {
	DRIVER_COMMON;
	devtab_t *devtab;

#define DEV_DEP_INTR	(1 << 0)
#define DEV_DEP_DYN	(1 << 1) /* Dynamically allocated resources */
#define DEV_NO_EARLY	(1 << 2)
#define DEV_EARLY 	(1 << 3)
	int flags;

	size_t private; /* sizeof a driver private structure */
	device_type_t type;

	/**
	 * @brief Probe if the correct drier was loaded.
	 * @retval DEVICE_PROBE_OK
	 * @retval DEVICE_PROBE_FAIL
	 * @retval DEVICE_PROBE_LOPRIO
	 */
	int (*probe) (device_t *);

	/**
	 * @brief Attach a device.
	 * @retval 0		Sucess.
	 * @retval -ENODEV 	No device present here, free the device.
	 * @retval < 0		Other errors.
	 */
	int (*attach) (device_t *);

	/**
	 * @brief Detach a device.
	 * @retval 0		Success.
	 * @retval -EBUSY	The device cannot be detached.
	 */
	int (*detach) (device_t *);

	/**
	 * @brief Perform a new pass through the device tree.
	 */
	void (*new_pass) (device_t *);

	/**
	 * @brief Call a device specific method.
	 */
	int (*mthd) (device_t *, int mthd, void *arg);

	/**
	 * @brief The bus callbacks of a device (if the device is a bus).
	 */
	struct bus_driver *bus;
} device_driver_t;

/**
 * @brief A callback for matching two device ids.
 * @param devid 	The actual ID of the device.
 * @param devtab_id	The device ID in the device table.
 */
typedef bool (*device_id_match_t) (uint64_t devid, uint64_t devtab_id);

#define BUS_MAX16 0xFFFF
#define BUS_MAX32 0xFFFFFFFF
#define BUS_SIZE_MAX BUS_ADDR_MAX
#define BUS_OFF_MAX BUS_ADDR_MAX
typedef bus_addr_t bus_size_t;
typedef bus_addr_t bus_off_t;

#define BUS_DEV_ADDR_ANY UINT64_MAX
typedef uint64_t bus_dev_addr_t;

/*
 * Some definitions are offloaded to some external headers for readability.
 */
#include <device/_resource.h>
#include <device/_property.h>
#include <device/_mthd.h>

/**
 * @brief Bus callbacks
 */
typedef struct bus_driver {
	void (*bus_child_free) (device_t *, device_t *);
	void (*bus_child_detach) (device_t *, device_t *);
	int (*bus_alloc_res) (device_t *, bus_res_req_t *req);
	void (*bus_free_res) (device_t *, bus_res_t *);
	int (*bus_setup_res) (device_t *, bus_res_t *);
	void (*bus_teardown_res) (device_t *, bus_res_t *);
} bus_driver_t;

extern sync_t device_lock;
extern device_t *root_device;

/**
 * @brief Used for implementing bus_foreach_child.
 */
device_t *bus_child_next(device_t *bus, device_t *prev);

/**
 * @brief Get the driver of a device.
 */
device_driver_t *device_driver(device_t *device);

/**
 * @brief Get the name of a device.
 */
const char *device_name(device_t *device);

/**
 * @brief Get the bus address of a device.
 */
bus_dev_addr_t device_addr(device_t *device);

#define device_printf(dev, fmt, ...) ({					\
	kprintf("[%s@0x%llx] " fmt, device_name(dev), device_addr(dev),	\
		## __VA_ARGS__);					\
})

/**
 * @brief Return the pointer to the structure allocated by device_alloc_priv
 * @see device_alloc_priv
 */
void *device_priv(device_t *device);

/**
 * @brief Check whether a device is attached or not.
 */
bool device_attached(device_t *device);

/**
 * @brief Allocate a per-device driver specific structure
 *
 * A per-device driver specific structure is being allocated
 * without initializing the memory. There is no need to manually
 * free this structure, even if the attach fails.
 *
 * @param device 	the device to allocate the structure for
 * @param size 		the size of the structure
 * @return 		the pointer to the structure on success
 */
void *device_alloc_priv(device_t *device, size_t size);

/**
 * @brief Add a description to a device
 * @param device 	the device being described
 * @param fmt 		the description in a kprintf type format string
 */
void device_set_desc(device_t *device, const char *fmt, ...);

/**
 * @brief Returns a pointer to the parent device
 */
device_t *device_get_parent(device_t *device);

/**
 * @brief Returns the interrupt parent of the device
 */
device_t *device_get_intr_parent(device_t *device);

/**
 * @brief Sets the interrupt parent of the device
 */
void device_set_intr_parent(device_t *device, device_t *intr_parent);

/**
 * @brief Set a device flag.
 */
void device_set_flag(device_t *device, int flag);

/**
 * @brief Call a device specific method.
 */
int device_mthd(device_t *device, int mthd, void *arg);

/**
 * @brief Choose a driver for a device and initialize it
 *
 * If the driver returns -ENODEV to indicate that there is no hardware
 * present for a device, device_probe_attach may free the device! Keep
 * that in mind when calling this function e.g. inside foreach.
 */
int device_probe_attach(device_t *device);

/**
 * @brief Default implementation for match callback of device_add_id
 */
bool device_generic_match_id(uint64_t devid, uint64_t devtab_id);

/**
 * @brief Add an id to the device (used to choose drivers using devtabs)
 */
void device_add_id(device_t *device, const char *type, uint64_t num,
	device_id_match_t match);

/**
 * @brief Inrease the number of devices which depend on this device
 *
 * If the ref-count is greater than 0 the device cannot be detached
 * (device_detach will return -EBUSY). This is used by interrupt
 * controllers for example, because the interrupt children have to
 * be detached first before the controller can be detached.
 *
 * @param device The device which is being referenced
 *
 */
void device_add_ref(device_t *device, device_t *ref);

/**
 * @brief Decrease the number of devices which depend on this device
 *
 * @param device The device which is being referenced
 */
void device_remove_ref(device_t *device, device_t *ref);

device_t *device_find_type(device_t *start, device_type_t type,
	const char *driver);

/**
 * @brief Call the new_pass callback for the device
 */
void device_new_pass(device_t *device);

/**
 * @brief TODO
 */
device_t *bus_device_parent(device_t *device, bus_res_type_t type);

/**
 * @brief Get a child device of a bus at a specific address.
 */
device_t *bus_get_child(device_t *bus, bus_dev_addr_t addr);

/**
 * @brief Call the bus_alloc_res callback of a bus.
 */
static inline int BUS_ALLOC_RES(device_t *bus, bus_res_req_t *req) {
	return device_driver(bus)->bus->bus_alloc_res(bus, req);
}

/**
 * @brief Call the bus_free_res callback of a bus.
 */
static inline void BUS_FREE_RES(device_t *bus, bus_res_t *res) {
	device_driver(bus)->bus->bus_free_res(bus, res);
}

/**
 * @brief Call the bus_free_res callback of a bus.
 */
static inline int BUS_SETUP_RES(device_t *bus, bus_res_t *res) {
	return device_driver(bus)->bus->bus_setup_res(bus, res);
}

/**
 * @brief Call the bus_free_res callback of a bus.
 */
static inline void BUS_TEARDOWN_RES(device_t *bus, bus_res_t *res) {
	device_driver(bus)->bus->bus_teardown_res(bus, res);
}

/**
 * @brief The default implementation of the detach device callback for busses
 */
int bus_generic_detach(device_t *device);

/**
 * @brief The default implementation of a bus_alloc_res driver callback
 *
 * Drivers may use this value as an implementation or as a part of their
 * implementation of the bus_alloc_res callback in the bus interface.
 * This function simply passes the request up the device tree.
 */
int bus_generic_alloc_res(device_t *bus, bus_res_req_t *req);

/**
 * @brief The default implementation of the bus_free_res bus-callback!
 */
static inline void bus_generic_free_res(device_t *bus, bus_res_t *res) {
	assert(res->setup == false);
	sync_assert(&device_lock);
	BUS_FREE_RES(bus_device_parent(bus, res->type), res);
}

/**
 * @brief The default implementation of the bus_setup_res bus-callback!
 */
static inline int bus_generic_setup_res(device_t *bus, bus_res_t *res) {
	/*
	 * TODO Maybe allow enabling/disabling interrupts and
	 * mapping/unmapping IO/mem-resources without holding
	 * the device_lock in the future!!!
	 */
	sync_assert(&device_lock);
	return BUS_SETUP_RES(bus_device_parent(bus, res->type), res);
}

/**
 * @brief The default implementation of the bus_teardown_res bus-callback!
 */
static inline void bus_generic_teardown_res(device_t *bus, bus_res_t *res) {
	sync_assert(&device_lock);
	BUS_TEARDOWN_RES(bus_device_parent(bus, res->type), res);
}

/**
 * @brief The default implementation of the new_pass bus-callback!
 */
void bus_generic_new_pass(device_t *bus);

/**
 * @brief Probe & attach the child devices, added by bus_add_child
 */
void bus_probe_children(device_t *bus);

/**
 * @brief Add a child device to a bus!
 */
device_t *bus_add_child_at(device_t *bus, const char *drv_hint,
	bus_dev_addr_t addr);

static inline device_t *bus_add_child(device_t *bus, const char *hint) {
	return bus_add_child_at(bus, hint, BUS_DEV_ADDR_ANY);
}

/**
 * @brief	Free all children from a bus (the children may have been not be
 *		attached yet)
 */
void bus_remove_children(device_t *bus);

/**
 * Remove a child device to a bus. This can only be used
 * when the device has not been probed yet (i.e. child->state is
 * DEVICE_REGISTER). Don't confuse this with detach!
 */
void bus_remove_child(device_t *bus, device_t *child);

/**
 * @brief Bind a device with a bus-specific structure
 *
 * In thee bus_child_free callback a bus driver should call
 * bus_set_child_priv(child, NULL) or bus_free_child_priv(child)
 * if it provided a private pointer for a child!
 */
void bus_set_child_priv(device_t *device, void *priv);

/**
 * Allocate a bus-specific structure for a given device
 * This allocation is not freed automatically and
 * has to be freed using bus_free_child_priv
 * in the bus_child_free callback
 */
void *bus_alloc_child_priv(device_t *device, size_t size);

/**
 * @brief Free the memory allocated using bus_alloc_child_priv
 */
void bus_free_child_priv(device_t *device);

/**
 * @brief Get the bus private pointer of a device.

 * Get the bus private pointer of a device provided by
 * device_set_bus_priv or device_alloc_bus_priv.
 */
void *bus_child_priv(device_t *device);

void init_device(void);
void init_device_early(void);
void arch_device_init(void);
void device_driver_init(void);

#endif

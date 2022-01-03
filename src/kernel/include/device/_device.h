#ifndef DEVICE__DEVICE_H
#define DEVICE__DEVICE_H

/*
 * Private definitions of the device interface.
 * Do not inlude this header in your driver.
 */
 typedef enum device_state {
 	DEVICE_REGISTER = 0,
 	DEVICE_PROBE,
 	DEVICE_ATTACH,
 	DEVICE_ATTACH_LATER,
 } device_state_t;

 typedef struct device_id {
 	struct device_id *next;
 	const char *type;
 	uint64_t num;
 	device_id_match_t match;
 } device_id_t;

 struct device {
 	list_node_t node;
 	char desc[48];

 	int flags;
 	device_state_t state;

 	void *priv;
 	size_t num_ref; /* Number of times devices depend on this device */
 	size_t num_res; /* Number of resources this device allocated */

 	device_t *parent; /* constant */
 	void *bus_priv; /* Parent may store information in a child */

 	list_node_t node_child;
 	list_t children;

 	struct bus_dma_engine *dma;

 	bus_dev_addr_t addr; /* Bus specific address for the device. */

 	/*
 	 * If the device has a intr_parent all the BUS_RES_IRQ request
 	 * are being redirected to intr_parent else the normal parent
 	 * is asked.
 	 */
 	device_t *intr_parent;
 	list_t intr_children;
 	list_node_t node_intr;

 	const char *driver_hint; /* constant, may be NULL */
 	device_driver_t *driver;

 	/*
 	 * The id which actually caused the probe/attach.
 	 */
 	devtab_entry_t *devtab_entry;

 	/*
 	 * The ids identifing the device.
 	 */
 	device_id_t *ids;

 	/*
 	 * The properties of the device.
 	 */
 	list_t proplist;
};

/**
 * @brief Allocate a new device structure.
 */
device_t *device_alloc(const char *drv_hint);

/**
 * @brief Free a device structure.
 *
 * The device being freed has to be in the DEVICE_REGISTER state and
 * not be a child of the parent bus. However it may still be a child
 * of the interrupt parent and will be removed from the interrupt
 * parent if necessary.
 */
void device_free(device_t *device);

/**
 * @brief Add a child device to a bus.
 */
void bus_insert_child(device_t *bus, device_t *child, bus_dev_addr_t at);

/**
 * @brief Detach a device
 *
 * This function calls the devices detach callback (which has to free all the
 * resources and make sure that there is no more access to the device!) and
 * informs the parent of the detach using _bus_on_child_detach_. After that the
 * devices private pointer is freed using kfree.
 *
 * @param device 	the device to detach
 * @return 		-EBUSY when the driver cannot be detached
 *			0 when the driver successfully has been detached
 */
int device_detach(device_t *device);

/**
 * Compare ids from the devtab with all of the device's id provided by
 * device_add_id until a match is found.
 *
 * @see 	device_add_id
 * @return 	The first entry matching
 *		NULL if no match was found
 */
devtab_entry_t *device_match_devtab(device_t *device, devtab_t *devtab);

/**
 * @brief Increase the number of resources this device allocated
 */
void device_inc_res_num(device_t *device);

/**
 * @brief Decrease the number of resources this device allocated
 */
void device_dec_res_num(device_t *device);

/**
 * @brief Choose a driver for a device.
 */
int device_driver_choose(device_t *device);

/**
 * @brief  Informs a bus that a child has been detached.
 *
 * @param device	The device (parent of child) that will be informed
 * @param child 	The device that has been detached
 */
void bus_on_child_detach(device_t *device, device_t *child);

/**
 * @brief Informs a bus that a child device is going to be freed
 *
 * Informs a bus that a child device is going to be freed, which usually happens
 * when the bus detaches.
 */
void bus_on_child_free(device_t *device, device_t *child);

#endif

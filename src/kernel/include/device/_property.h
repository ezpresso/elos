#ifndef DEVICE__PROPERTY_H
#define DEVICE__PROPERTY_H

/**
 * @brief The type of a device property.
 */
typedef enum devprop_type {
	DEVPROP_STR = 0,
	DEVPROP_NUM,
	DEVPROP_RES,
	DEVPROP_OTHER,
	DEVPROP_ANY,
} devprop_type_t;

#define DEVPROP_STOP DEVPROP_ANY

/**
 * @brief A property describing a resource.
 */
typedef struct devprop_res {
	bus_addr_t addr;
	bus_size_t size;
	bus_res_type_t type;
} devprop_res_t;

/**
 * @brief A device property.
 */
typedef struct devprop {
	list_node_t node;
	devprop_type_t type;
	const char *name;
	
#define DEVPROP_DETACH (1 << 0) /* Property freed on detach */
	int flags;

	union {
		devprop_res_t res;
		uint64_t num;
		void *ptr;
		const char *str;	
	} value;
} devprop_t;

#define _DEVPROP(pname, ptype, value...) \
	{ .type = DEVPROP_ ## ptype, .name = pname, value }

#define DEVPROP_RES(name, rtype, raddr, rsize)	\
	_DEVPROP(name, RES, .value.res = {	\
		.addr = raddr,			\
		.size = rsize,			\
		.type = BUS_RES_ ## rtype,	\
	})

#define DEVPROP_NUM(name, number) \
	_DEVPROP(name, NUM, .value.num = number)

#define DEVPROP_STR(name, string) \
	_DEVPROP(name, STR, .value.str = string)

#define DEVPROP_PTR(name, pointer) \
	_DEVPROP(name, STR, .value.ptr = pointer)

#define DEVPROP_END() _DEVPROP(NULL, STOP, .value.num = 0)

devprop_t *devprop_alloc(device_t *device, const char *name,
	devprop_type_t type);

static inline void device_prop_add(device_t *device, const char *name,
	void *ptr)
{
	devprop_t *prop = devprop_alloc(device, name, DEVPROP_OTHER);
	if(prop) {
		prop->value.ptr = ptr;
	}
}

static inline void device_prop_add_num(device_t *device, const char *name,
	uint64_t num)
{
	devprop_t *prop = devprop_alloc(device, name, DEVPROP_NUM);
	if(prop) {
		prop->value.num = num;
	}
}

static inline void device_prop_add_str(device_t *device, const char *name,
	const char *str)
{
	devprop_t *prop = devprop_alloc(device, name, DEVPROP_STR);
	if(prop) {
		prop->value.str = str;
	}
}

static inline void device_prop_add_res(device_t *device, const char *name,
	bus_res_type_t type, bus_addr_t addr, bus_size_t size)
{
	devprop_t *prop = devprop_alloc(device, name, DEVPROP_RES);
	if(prop) {
		prop->value.res.type = type;
		prop->value.res.addr = addr;
		prop->value.res.size = size;
	}
}

devprop_t *device_prop_get(device_t *device, devprop_type_t type,
	const char *name);

static inline void *device_prop(device_t *device, const char *name) {
	devprop_t *prop = device_prop_get(device, DEVPROP_OTHER, name);
	if(prop) {
		return prop->value.ptr;
	} else {
		return NULL;
	}
}

static inline int device_prop_num(device_t *device,
	const char *name, uint64_t *num)
{
	devprop_t *prop = device_prop_get(device, DEVPROP_NUM, name);
	if(prop) {
		*num = prop->value.num;
		return 0;
	} else {
		return -ENOENT;
	}
}

static inline const char *device_prop_str(device_t *device,
	const char *name)
{
	devprop_t *prop = device_prop_get(device, DEVPROP_STR, name);
	if(prop) {
		return prop->value.str;
	} else {
		return NULL;
	}
}

devprop_res_t *device_prop_res(device_t *device, bus_res_type_t type,
	const char *name);

#endif
#ifndef DEVICE_TREE_H
#define DEVICE_TREE_H

/*
 * Some helpers for creating the device tree in motherboard drivers.
 */

typedef struct devtree_state {
	device_t *parents[16];
	device_t *dev;
	size_t cur;
} devtree_state_t;

static inline void __devtree_cleanup(size_t **ptr) {
	(**ptr)--;
}

#define __devtree_push_parent(parent) ({			\
	assert(_state.cur + 1 < NELEM(_state.parents));		\
	_state.parents[++_state.cur] = parent;			\
})

#define __devtree_scope_begin(n)				\
	__devtree_push_parent(n);				\
	for(size_t * __cleanup(__devtree_cleanup) __unused	\
		__devtree_var = &_state.cur, 			\
		__devtree_tmp = 1; __devtree_tmp != 0; __devtree_tmp = 0)

#define devtree_cur _state.parents[_state.cur]

#define devtree_define(device, code...) ({			\
	devtree_state_t _state;					\
	_state.cur = 0;						\
	devtree_cur = device;					\
	code;							\
	0;							\
})

#define device(drv, addr) 					\
	_state.dev = bus_add_child_at(devtree_cur, drv, addr);	\
	device_set_flag(_state.dev, DEVICE_PERM);		\
	__devtree_scope_begin(_state.dev)

#define decldev(name, drv)					\
	device_t * name = device_alloc(drv);			\
	name->flags = DEVICE_PERM;

#define impldev(name, a)					\
	bus_insert_child(devtree_cur, name, a)			\
	__devtree_scope_begin(name)

#define intr_parent(dev) \
	device_set_intr_parent(devtree_cur, dev)

#define intr_root() \
	devtree_cur->flags |= DEVICE_INTR_ROOT

#define io(name, addr, size) \
	device_prop_add_res(devtree_cur, name, BUS_RES_IO, addr, size)

#define intr(name, addr, size) \
	device_prop_add_res(devtree_cur, name, BUS_RES_INTR, addr, size)

#define mem(name, addr, size) \
	device_prop_add_res(devtree_cur, name, BUS_RES_MEM, addr, size)

#endif

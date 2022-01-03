#ifndef ELOS_DRIVER_H
#define ELOS_DRIVER_H

#include <kern/section.h>
#include <lib/list.h>

#define DRIVER_SECTION	section(driver, driver_entry_t)
#define __driver	section_entry(DRIVER_SECTION)

typedef enum driver_type {
	DRIVER_DEVICE = 0,
	DRIVER_FILESYS,
} driver_type_t;

typedef struct driver_entry {
	driver_type_t type;
	struct module *module; /* TODO */
	void *driver;

	list_node_t node;
	size_t ref;
	size_t hash;
} driver_entry_t;

/**
 * @brief The fields at the beginning of every driver.
 */
#define DRIVER_COMMON			\
	driver_entry_t *drv_entry;	\
	const char *name		\

#define driver(drv, t)						\
	static __driver driver_entry_t driver ## _private = {	\
		.type = t,					\
		.driver = drv,					\
		.module = NULL,					\
	}

void *driver_get(const char *name, driver_type_t type);
void driver_put(void *driver);

void driver_add(driver_entry_t *entry);
int driver_remove(driver_entry_t *entry);

#endif
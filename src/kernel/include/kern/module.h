#ifndef KERN_MODULE_H
#define KERN_MODULE_H

#ifndef __MODULE__
#define MODULE_NAME "kernel"
#else
#define MODULE_NAME __MODULE_NAME__
#endif

#define MODULE(ld, unld) 			\
	module_t module ## n = {		\
		.name = MODULE_NAME,		\
		.kern_major = KERN_MINOR	\
		.kern_minor = KERN_MAJOR	\
		.load = ld,			\
		.unload = unld,			\
	};					\

#if 0
#define MODULE_DEPENDS(mod)
#endif

typedef struct module {
	const char *name;
	int (*load) 	(void);
	int (*unload)	(void);

	unsigned kern_major;
	unsigned kern_minor;
} module_t;

int module_load(const char *name);

#if 0
int module_unload(...);
void module_add_path(...);
#endif

#endif
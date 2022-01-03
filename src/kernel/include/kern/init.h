#ifndef KERN_INIT_H
#define KERN_INIT_H

typedef struct initcall {
	int (*func) (void);
	char *name;
} initcall_t;

#define INIT_EARLY	0
#define INIT_FS		1
#define INIT_DEV	2
#define INIT_LATE	3
#define INIT_FINISHED	4

#define INIT_PANIC	-2 /* Unrecoverable error */
#define INIT_ERR	-1 /* Error, but don't care */
#define INIT_OK		0

#define __init 		__section(".initcode")
#define __initdata	__section(".initdata")

#define define_initcall(level, function) 				\
	static __used initcall_t UNIQUE_NAME(__init_## function) 	\
		__section(".initcall" level ".init") =			\
		{ .func =  function, .name = # function }

#define early_initcall(func)	define_initcall("0", func)
#define fs_initcall(func)	define_initcall("1", func)
#define dev_initcall(func)	define_initcall("2", func)
#define late_initcall(func)	define_initcall("3", func)

int init_get_level(void);
void init_level(uint8_t level);
void init_free(void);

#endif

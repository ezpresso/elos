#ifndef KERN_SECTION_H
#define KERN_SECTION_H

/* Example usage:
 *
 * #define TESTS section(test, test_t)
 * #define __test section_entry(TESTS)
 *
 * typedef struct test {
 *     char *ch;
 * } test_t;
 *  
 * // in file1.c
 * (may be static) __test test_t file1_test = {
 *     .ch = "Hello from file1",
 * };
 *
 * // in file2.c
 * (may be static) __test test_t file2_test = {
 *     .ch = "Hello from file1",
 * };
 *
 * // in some function
 * test_t *cur;
 *
 * section_foreach(cur, TESTS) {
 *    kprintf("%s\n", cur->ch);
 * }
 * 
 * // Might print out something like this:
 *  Hello from file1
 *  Hello from file2
 *
 */

#define section(name, type) (name, type)

#define __section_get_name(sec) __section_extract_name sec
#define __section_extract_name(name, type) name
#define __section_get_type(sec) __section_extract_type sec
#define __section_extract_type(name, type) type

#define __section_name(sec)  CONCAT(section_ ,  __section_get_name(sec))

#define section_entry(sec) \
	__attribute__((__section__(STR(__section_name(sec))), aligned(8), used))

#define section_foreach(name, sec)	\
	for(name = section_first(sec);	\
		name < section_end(sec);	\
		name = (__typeof__(name))ALIGN_PTR((void *)((name) + 1), 8))

/* Linker automatically creates variables for start and stop of section */
#define section_linker_start(sec) CONCAT(__start_, __section_name(sec))
#define section_linker_stop(sec)  CONCAT(__stop_, __section_name(sec))

#define secton_nelem(sec) ({						\
	extern __section_get_type(sec) section_linker_start(sec);	\
	extern __section_get_type(sec) section_linker_stop(sec);	\
	((__section_get_type(sec) *) &section_linker_stop(sec)) -	\
		((__section_get_type(sec) *) &section_linker_start(sec)); \
})

#define section_size(sec) ({						\
	extern __section_get_type(sec) section_linker_start(sec);	\
	extern __section_get_type(sec) section_linker_stop(sec);	\
	((uintptr_t) &section_linker_stop(sec)) -			\
		((uintptr_t) &section_linker_start(sec)); 		\
})

#define section_first(sec) ({						\
	extern __section_get_type(sec) section_linker_start(sec);	\
	(__section_get_type(sec) *)& section_linker_start(sec);		\
})

#define section_end(sec) ({						\
	extern __section_get_type(sec) section_linker_stop(sec);	\
	(__section_get_type(sec) *)& section_linker_stop(sec);		\
})

#endif

#ifndef KERN_LOCAL_H
#define KERN_LOCAL_H

#include <kern/section.h>

/* Helpers for thread/process local storage */

#define define_local_var(sec, type, name) \
	section_entry(sec) type name

#define local_call_err(sec, type, func, pt, ...) ({		\
	type *local;						\
	int err = 0;						\
	section_foreach(local, sec) {				\
		if(local-> func == NULL) {			\
			continue;				\
		}						\
		err = local-> func (pt, ## __VA_ARGS__);	\
		if(err) {					\
			type *tmp;				\
			section_foreach(tmp, sec) {		\
				tmp->exit(pt);			\
			}					\
			break;					\
		}						\
	}							\
	err;							\
})

#define local_call(sec, type, func, pt, ...) ({			\
	type *local;						\
	section_foreach(local, sec) {				\
		if(local-> func) {				\
			local-> func (pt, ## __VA_ARGS__);	\
		}						\
	}							\
})

#define local_size(sec, type) ({	\
	size_t size = 0;		\
	type *local;			\
	section_foreach(local, sec) {	\
		local->off = size;	\
		size += local->size;	\
	}				\
	assert(size);			\
	size;				\
})

#endif
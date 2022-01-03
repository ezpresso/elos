#ifndef KERN_ENV_H
#define KERN_ENV_H

#include <kern/section.h>
#include <lib/list.h>

#define ENV_SECTION section(kernenv, env_var_t)
#define ENV_STRSZ 32

typedef enum env_var_type {
	ENV_INT = 0,
	ENV_UINT,
	ENV_STR,
	ENV_BOOL,
} env_var_type_t;

typedef struct env_var {
	list_node_t node;
	size_t hash;

	const char *name;
	env_var_type_t type;

#define ENV_LOCKED (1 << 0)
	uint8_t flags;

	union {
		char val_str[ENV_STRSZ];
		unsigned val_uint;
		int val_int;
	};
} env_var_t;

#define KERN_ENV_VAR(var, env, vtype, field, init)	\
	section_entry(ENV_SECTION) env_var_t var = {	\
		.name = env,				\
		.type = vtype,				\
		.field = init,				\
	};

#define KERN_ENV_INT(var, str, init) \
	KERN_ENV_VAR(var, str, ENV_INT, val_int, init)
#define KERN_ENV_UINT(var, str, init) \
	KERN_ENV_VAR(var, str, ENV_UINT, val_uint, init)
#define KERN_ENV_STR(var, str, init) \
	KERN_ENV_VAR(var, str, ENV_STR, val_str, init)
#define KERN_ENV_BOOL(var, str, init) \
	KERN_ENV_VAR(var, str, ENV_BOOL, val_int, init)

#define kern_var_assert_unlocked(var) \
	assert(((var)->flags & ENV_LOCKED));

#define kern_var_seti(var, value) ({	\
	kern_var_assert_unlocked(var);	\
	(var)->val_int = (value);	\
})

#define kern_var_setu(var, value) ({	\
	kern_var_assert_unlocked(var);	\
	(var)->val_uint = (value);	\
})

#define kern_var_sets(var, value) ({	\
	kern_var_assert_unlocked(var);	\
	_kern_var_sets(var, value);	\
})

#define kern_var_setb(var, value) ({		\
	bool __b = (value);			\
	kern_var_assert_unlocked(var);		\
	assert(__b == true || __b == false);	\
	(var)->val_int = __b;			\
})

#define kern_var_geti(var) (var)->val_int
#define kern_var_getu(var) (var)->val_uint
#define kern_var_gets(var) (var)->val_str
#define kern_var_getb(var) (var)->val_int

#define kern_env_seti(name, value) \
	kern_var_seti(kern_env_get(name), value)
#define kern_env_setu(name, value) \
	kern_var_setu(kern_env_get(name), value)
#define kern_env_sets(name, value) \
	kern_var_sets(kern_env_get(name), value)
#define kern_env_setb(name, value) \
	kern_var_setb(kern_env_get(name), value)
#define kern_env_geti(name) \
	kern_var_geti(kern_env_get(name))
#define kern_env_getu(name) \
	kern_var_getu(kern_env_get(name))
#define kern_env_gets(name) \
	kern_var_gets(kern_env_get(name))
#define kern_env_getb(name) \
	kern_var_getb(kern_env_get(name))

static inline void kern_var_lock(env_var_t *var) {
	var->flags |= ENV_LOCKED;
}

static inline void kern_var_unlock(env_var_t *var) {
	var->flags &= ~ENV_LOCKED;
}

void _kern_var_sets(env_var_t *var, const char *value);
env_var_t *kern_env_get(const char *name);

#endif
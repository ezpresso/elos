/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 * 
 * Copyright (c) 2017, Elias Zell
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, proided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <kern/system.h>
#include <kern/env.h>
#include <kern/init.h>
#include <lib/hashtab.h>
#include <lib/string.h>

#define ENV_HT_SIZE 1024

static bool env_init = false;
static hashtab_t env_ht;

void _kern_var_sets(env_var_t *var, const char *value) {
	strlcpy(var->val_str, value, ENV_STRSZ);
}

static env_var_t *env_get_early(const char *name) {
	env_var_t *cur;

	section_foreach(cur, ENV_SECTION) {
		if(!strcmp(cur->name, name)) {
			return cur;
		}
	}

	return NULL;
}

static env_var_t *env_get(const char *name) {
	env_var_t *var;
	size_t hash;

	hash = hash_str(name);
	hashtab_search(var, hash, &env_ht) {
		if(!strcmp(name, var->name)) {
			return var;
		}
	}

	return NULL;
}

env_var_t *kern_env_get(const char *name) {
	env_var_t *var;

	if(env_init) {
		var = env_get(name);
	} else {
		var = env_get_early(name);
	}

	if(var == NULL) {
		kpanic("unknown kernel environment variable: \"%s\"", name);
	} else {
		return var;
	}
}


void __init init_env(void) {
	env_var_t *cur;

	hashtab_alloc(&env_ht, ENV_HT_SIZE, VM_WAIT);
	section_foreach(cur, ENV_SECTION) {
		list_node_init(cur, &cur->node);
		cur->hash = hash_str(cur->name);
		hashtab_set(&env_ht, cur->hash, &cur->node);
	}

	env_init = true;
}

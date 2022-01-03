#ifndef KERN_ASYNC_H
#define KERN_ASYNC_H

#include <lib/list.h>

typedef void (*async_func_t) (void *arg);

typedef struct async {
	list_node_t node;
	async_func_t func;
	void *arg;
} async_t;

/**
 * @brief Call the function a little bit later in a safe environment.
 *
 * async_call can invoke a function which e.g. blocks even if the
 * current thread is currently in a critical section, because the
 * function is called by an extra thread. The async_t structure
 * is just needed to store some information. This structure is no
 * longer needed while and after @p func is called and may thus
 * be freed inside the call. 
 */
void async_call(async_t *call, async_func_t func, void *arg);

void init_async(void);

#endif
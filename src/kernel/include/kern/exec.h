#ifndef KERN_EXEC_H
#define KERN_EXEC_H

#include <lib/list.h>

struct thread;
struct vnode;
struct kstack;
enum vm_seg;

#define EXEC_OK		(0)
#define EXEC_INTERP	(1)
#define EXEC_NOMAG	(2)

typedef struct exec_img {
	const char *binary;

	struct vnode *node;
	struct vm_page *page;

	struct vm_vas *vas;

	void *header;
	uintptr_t entry;
	void *stackptr;

#define EXEC_SCRIPT	(1 << 0)
#define EXEC_FREEPATH	(1 << 1) /* Free the img->binary pointer */
	int flags;

	union {
		struct {
			uintptr_t phdr;
			uintptr_t entry;
			size_t phent;
			size_t phnum;
		} aux;
	};

	/* The memory available for the environment variables and the
	 * arguments. Argument strings are at the beginning.
	 */
	void *strmem;
	void *strptr;
	/* The space left mem */
	size_t strspace;

	/* TODO could remove env, cause it's simply args+argsize */
	union {
		char *env;
		const char **envvec;
	};

	size_t envc;
	size_t envsize;

	union {
		char *args;
		const char **argvec;
	};

	size_t argc;
	size_t argsize;
} exec_img_t;

typedef struct binfmt {
	list_node_t node;
	const char *name;

	/**
	 * @brief Try to load the executable.
	 *
	 * Check if the binary has a format known to the driver and load
	 * the file into memory.
	 *
	 * @retval EXEC_OK		Success.
	 * @retval EXEC_NOMAG	The executable cannot be loaded by this driver.
	 * @retval EXEC_INTERP	The caller exchanged the path to the binary
	 *			in the image structure by calling exec_interp
	 *			and this binary should be loaded instead the
	 *			current binary.
	 */
	int (*exec)	(exec_img_t *image);

	/**
	 * @brief Push the auxvals onto the stack.
	 *
	 * Called after a sucessful call to the exec callback of this
	 * binary format driver when initializing the stack.
	 */
	int (*initaux)	(exec_img_t *image, struct kstack *stack);
} binfmt_t;

/**
 * @brief Register a binary format driver.
 */
void binfmt_register(binfmt_t *binfmt);

/**
 * @brief Remove a binary format driver.
 */
void binfmt_unregister(binfmt_t *binfmt);

/**
 * @brief Initialize a struture describing an executable.
 */
int exec_init_img(exec_img_t *image, const char *path);

/**
 * @brief Free every resoure associated with an executable image.
 */
void exec_cleanup(exec_img_t *image);

/**
 * @brief Lookup the vnode of the executable and map the first page.
 */
int exec_lock_map_node(exec_img_t *image);

/**
 * @brief Interpret the current image.
 *
 * Unmap the vnode associated with the image and try executing the
 * node described by the path @p interp.
 *
 * @return	EXEC_INTERP on success
 *		< 0 on error
 */
int exec_interp(exec_img_t *img, const char *interp, size_t len);

/**
 * @brief Execute a file.
 */
int kern_execve(const char *path, const char **argv, const char **envp,
	enum vm_seg seg);

/**
 * @brief execve syscall.
 */
int sys_execve(const char *path, const char **argv, const char **env);

#endif

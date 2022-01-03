#ifndef VFS_DEV_H
#define VFS_DEV_H

#include <uapi/major.h>
#include <sys/sysmacros.h>

struct devfs_node;
struct file;
struct fops;

extern struct fops dev_ops;

/**
 * @brief Create a new device special file for a character device.
 */
int vmakechar(struct devfs_node *node, unsigned maj, mode_t mode,
	struct fops *fops, void *priv, dev_t *res, const char *name,
	va_list ap);

/**
 * @brief Create a new device special file for a character device.
 */
int makechar(struct devfs_node *node, unsigned maj, mode_t mode,
	struct fops *fops, void *priv, dev_t *res, const char *name, ...);

/**
 * @brief Create a new device special file for a block device.
 * TODO size
 */
int vmakeblk(struct devfs_node *node, unsigned maj, mode_t mode,
	size_t blksz_shift, struct fops *fops, void *priv, dev_t *res,
	const char *name, va_list ap);

/**
 * @brief Create a new device special file for a block device.
 * TODO size
 */
int makeblk(struct devfs_node *node, unsigned maj, mode_t mode,
	size_t blksz_shift, struct fops *fops, void *priv, dev_t *res,
	const char *name, ...);

/**
 * @brief Destory a devive special file.
 */
void destroydev(dev_t dev);

bool dev_match_fops(struct file *file, struct fops *fops);
bool dev_file_cmp(struct file *file, dev_t dev);
int dev_prot(dev_t dev);

#endif

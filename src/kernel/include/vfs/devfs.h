#ifndef VFS_DEVFS_H
#define VFS_DEVFS_H

struct devfs_node;

/**
 * @brief Create a new devfs node.
 */
struct devfs_node *devfs_mknodat(struct devfs_node *parent, dev_t dev,
	mode_t mode, size_t blksz_shift, const char *name);

/**
 * @brief Create a new devfs directory.
 */
struct devfs_node *devfs_mkdir(struct devfs_node *parent, const char *name);

/**
 * @brief Create a new devfs node.
 */
static inline struct devfs_node *devfs_mknod(dev_t dev, mode_t mode,
	size_t blksz_shift, const char *name)
{
	return devfs_mknodat(NULL, dev, mode, blksz_shift, name);
}

/**
 * @brief Remove a devfs node.
 */
void devfs_rmnod(struct devfs_node *node);

#endif
#ifndef VFS_VCACHE_H
#define VFS_VCACHE_H

#define VCACHE_MEMORY	(1 << MB_SHIFT)

struct filesys;
struct vnode;
struct vdirent;

/**
 * Get a vnode from the vnode cache (The filesystem has to be
 * locked).
 */
struct vnode *vnode_cache_lookup(struct filesys *fs, ino_t ino);

/**
 * Add a vnode to the vnode cache (The filesystem of the node
 * has to be locked).
 */
void vnode_cache_add(struct vnode *node);

/**
 * Tell the cache that a vnode hit reference count zero. Do not call
 * this function manually. It's invoked by vnode_unref.
 */
void vnode_cache_zeroref(struct vnode *node);

/**
 * Retrieve a vdirent from the vdirent cache. @p node->lock has to held while
 * calling this function and has to be held until vd_cache_put is called.
 */
struct vdirent *vdirent_cache_lookup(struct vnode *node, const char *name,
	size_t namelen);

/**
 * Tell the vcache that this dirent is not needed anymore. 
 */
void vdirent_cache_put(struct vnode *node, struct vdirent *dirent);

/**
 * Add a new dirent to the vdirent cache. @p node->lock has to held while
 * calling this function. 
 */
struct vdirent *vdirent_cache_new(struct vnode *node, const char *name,
	size_t namelen, ino_t ino, int flags);

/**
 * Add a dirent to the vdirent cache. @p node->lock has to held while calling
 * this function. 
 */
void vdirent_cache_add(struct vnode *node, struct vdirent *dirent);

/**
 * Try to lookup a directory entry in the cache and remove it if it was found.
 */
void vdirent_cache_try_rem(struct vnode *node, const char *name,
	size_t namelen);

/**
 * Remove a cached directory entry. After calling this, the caller must not call
 * vd_cache_put.
 */
void vdirent_cache_rem(struct vnode *node, struct vdirent *dirent);


/**
 * @brief Initialize the vcache.
 */
void vcache_init(void);

#endif
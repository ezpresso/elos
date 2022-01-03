#ifndef VFS_PROC_H
#define VFS_PROC_H

struct file;
struct vpath;

#define VFS_APPLY_UMASK(mode) \
	(((mode) & ~0xFFF) | (((mode) & 0xFFF) & ~vfs_get_umask()))

/**
 * @brief Check if a path refers to the root directory of the current process.
 */
bool vfs_is_root(struct vpath *path);

/**
 * @brief Get the root directory of the current process.
 */
void vfs_get_root(struct vpath *path);

/**
 * @brief Set the root directory of the current process.
 */
void vfs_set_root(struct vpath *path);

/**
 * @brief Get the working directory of the current process.
 */
void vfs_get_cwd(struct vpath *path);

/**
 * @brief Set the working directory of the current process.
 */
void vfs_set_cwd(struct vpath *path);

/**
 * @brief Get the umask of the current process.
 */
mode_t vfs_get_umask(void);

/**
 * @brief Set the umask of the current process, while returning the old mask.
 */
mode_t vfs_set_umask(mode_t mode);

/**
 * @brief Allocate a new file descriptor
 *
 */
int fdalloc(struct file *file, bool cloexec, int min);

/**
 * @brief Allocate 2 file descriptors for 2 files.
 */
int fdalloc2(struct file *files[2], bool cloexec, int fd[2]);

/**
 * @brief Close a file descriptor obtained using fdalloc
 */
int fdfree(int fd);

/**
 * Allocate a file descriptor at newfd, by
 * closing the file descriptor at newfd if
 * necessary (used to implement dup2)
 *
 * @return 	Returns the new file destriptor on success
 *		or -EBADF if @p newfd is invalid
*/
int fddup(struct file *file, int newfd);

/**
 * @brief Get the file from a file descriptor
 */
struct file *fdget(int fd);

/**
 * @brief Check whether CLOEXEC flag is set for a file descriptor
 * @retval -EBADF	if @p fd is invalid
 * @retval false	if the CLOEXEC flag is not set
 * @retval true		if the CLOEXEC flag is set
 */
int fd_cloexec_get(int fd);

/**
 * @brief Update the CLOEXEC flag of a file descriptor
 * @retval -EBADF	if @p fd is invalid
 * @retval 0		on success
 */
int fd_cloexec_set(int fd, bool cloexec);

#endif

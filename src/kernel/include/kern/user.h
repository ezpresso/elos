#ifndef KERN_USER_H
#define KERN_USER_H

struct iovec;

int user_io_check(const void *buf, size_t size, size_t *maxsize);

/**
 * @brief Copy a buffer from user- to kernelspace.
 */
int copyin(void *kbuf, const void *ubuf, size_t size);

/**
 * @brief Copy a buffer from kernel to userspace.
 */
int copyout(void *ubuf, const void *kbuf, size_t size);

/**
 * @brief Copy a string from userspace into a buffer.
 */
int copyinstr(char *buf, const char *str, size_t bufsz, size_t *size);

/**
 * @brief	Copy a filesystem path from userspace into a freshly
 *		allocated buffer.
 */
int copyin_path(const char *ustr, char **out);

/**
 * @brief 	Copy a null terminated array of strings from
 *		userspace.
 */
int copyinstrvec(const char *uvec[], char ***out, size_t *nvec);

/**
 * @brief Set @p count bytes from a user buffer to @p value.
 */
int umemset(void *buf, int value, size_t count);

/**
 * @brief	Atomically copy user memory to kernel memory (supported sizes:
 *		1, 2, 4 and 8)
 */
int copyin_atomic(void *buf, const void *ubuf, size_t size);

/**
 * @brief	Atomically copy kernel memory to user memory (supported sizes:
 *		1, 2, 4 and 8)
 */
int copyout_atomic(void *ubuf, const void *buf, size_t size);

#endif
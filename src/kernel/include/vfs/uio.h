#ifndef VFS_IOV_H
#define VFS_IOV_H

#include <sys/uio.h>

struct kdirent;
struct vm_page;

typedef enum uio_rw {
	UIO_WR,
	UIO_RD,
} uio_rw_t;

typedef struct uio {
	struct iovec *iov;
	size_t iovc;
	off_t off;
	size_t size;
	uio_rw_t rw;
#define UIO_OFF		(1 << 0)
#define UIO_KERN	(1 << 1)
#define UIO_USER	(0)
	int flags;
} uio_t;

/**
 * @brief Initialize an uio structure
 *
 * The rw, off and flags field are initialized here, whereas the
 * iov, iovc and size fields are not.
 */
static inline void uio_init(uio_t *uio, off_t off, int flags, uio_rw_t rw) {
	uio->rw = rw;
	uio->flags = flags;

	if(off == -1) {
		uio->off = 0;
	} else {
		kassert(off >= 0, "[uio] negative offset: %lld", off);
		uio->off = off;
		uio->flags |= UIO_OFF;
	}
}

static inline void uio_simple(uio_t *uio, struct iovec *iov, off_t off,
		int flags, uio_rw_t rw)
{
	uio->iovc = 1;
	uio->iov = iov;
	uio->size = iov->iov_len;
	uio_init(uio, off, flags, rw);
}

/**
 * @brief Copy memory into or out of the uio's iovec.
 * @return -EFAULT on error and number of bytes moved on success.
 */
ssize_t uiomove(void *buf, size_t size, uio_t *uio);

/**
 * @brief Copy memory from a physical page to an uio or vice versa.
 * @return -EFAULT on error and number of bytes moved on success.
 */
ssize_t uiomove_page(struct vm_page *page, size_t size, size_t pgoff,
		uio_t *uio);

/**
 * @brief Copy an iovec into a kernel buffer
 */
int copyinuio(struct iovec *iov, size_t iovc, uio_t **uio);

/**
 * @brief Set every byte from an uio to @p value.
 */
int uiomemset(uio_t *uio, size_t size, int value);

/**
 * @brief Copy a directory entry into an uio.
 */
int uiodirent(struct kdirent *dent, const char *name, size_t namelen,
		uio_t *uio);

#endif
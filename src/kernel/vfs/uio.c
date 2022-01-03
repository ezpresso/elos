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
 * purpose with or without fee is hereby granted, provided that the above
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
#include <kern/user.h>
#include <lib/string.h>
#include <vm/malloc.h>
#include <vm/phys.h>
#include <vm/kern.h>
#include <vfs/uio.h>
#include <sys/limits.h>
#include <sys/dirent.h>

int copyinuio(struct iovec *uiov, size_t iovc, uio_t **out) {
	size_t iov_size = sizeof(struct iovec) * iovc;
	uio_t *uio;
	int err;

	if(iovc >= IOV_MAX) {
		return -EINVAL;
	}

	uio = kmalloc(sizeof(*uio) + iov_size, VM_NOFLAG);
	if(uio == NULL) {
		return -ENOMEM;
	}

	uio->iov = (void *)uio + sizeof(*uio);
	err = copyin(uio->iov, uiov, iov_size);
	if(err) {
		kfree(uio);
		return err;
	}

	/*
	 * Check if the total size is smalled than SIZE_MAX.
	 */
	uio->size = 0;
	for(size_t i = 0; i < iovc; i++) {
		if(uio->iov[i].iov_len > SIZE_MAX - uio->size) {
			kfree(uio);
			return -EINVAL;
		}

		uio->size += uio->iov[i].iov_len;
	}

	uio->iovc = iovc;
	uio->off = 0;
	uio->flags = UIO_USER;
	uio->rw = -1;
	*out = uio;

	return 0;
}

ssize_t uiomove(void *buf, size_t size, uio_t *uio) {
	size_t num, before;
	struct iovec *iov;
	int err;

	before = uio->size;
	assert(uio->off >= 0);

	while(size && uio->size) {
		iov = uio->iov;
		if(iov->iov_len == 0) {
			uio->iov++;
			uio->iovc--;
			continue;
		}

		err = 0;
		num = min(size, iov->iov_len);
		if(uio->rw == UIO_WR) {
			if(uio->flags & UIO_KERN) {
				memcpy(buf, iov->iov_base, num);
			} else {
				err = copyin(buf, iov->iov_base, num);
			}
		} else {
			if(uio->flags & UIO_KERN) {
				memcpy(iov->iov_base, buf, num);
			} else {
				err = copyout(iov->iov_base, buf, num);
			}
		}

		if(err) {
			/*
			 * TODO do such errors break the file offset?
			 */
			return err;
		}

		iov->iov_len -= num;
		iov->iov_base += num;
		uio->size -= num;
		uio->off += num;
		buf += num;
		size -= num;
	}

	/*
	 * Calc the number of bytes written/read.
	 */
	return before - uio->size;
}

ssize_t uiomove_page(struct vm_page *page, size_t size, size_t pgoff,
	uio_t *uio)
{
	vm_paddr_t phys = vm_page_phys(page);
	ssize_t retv;
	void *ptr;

	/*
	 * Cannot use vm_kern_map_quick, becuase uiomove might access
	 * user-memory, which would trigger a page fault and the page-
	 * fault handler might need the per-cpu quick page.
	 *
	 * TODO if vm_kern_map_quick could allocate another page when nested
	 * calls to vm_kern_map_phys happen and still pin the thread to
	 * this cpu while using the buffer, no tlb-invalidation IPI would
	 * need to be sent. This would save some time.
	 * MAYBE consider adding a VM_MAP_TEMP flag, which can do this.
	 */
	vm_kern_map_phys(phys, PAGE_SZ, VM_WAIT | VM_PROT_RW, &ptr);
	retv = uiomove(ptr + pgoff, size, uio);
	vm_kern_unmap_phys(ptr, PAGE_SZ);

	return retv;
}

int uiomemset(uio_t *uio, size_t size, int value) {
	struct iovec *iov;
	size_t len;
	int err;

	assert(uio->off >= 0);
	assert(uio->rw == UIO_RD);
	assert(size <= uio->size);

	while(size) {
		iov = uio->iov;
		if(iov->iov_len == 0) {
			assert(uio->iovc);
			uio->iov++;
			uio->iovc--;
			continue;
		}

		len = min(iov->iov_len, size);
		if(len == 0) {
			continue;
		}

		if(uio->flags & UIO_KERN) {
			memset(iov->iov_base, value, len);
		} else {
			err = umemset(iov->iov_base, value, len);
			if(err) {
				return err;
			}
		}

		iov->iov_len -= len;
		iov->iov_base += len;
		uio->size -= len;
		uio->off += len;
		size -= len;
	}

	return 0;
}

int uiodirent(struct kdirent *dent, const char *name, size_t namelen,
	uio_t *uio)
{
	size_t done = 0;
	int res;

	res = uiomove(dent, DIRENT_SZ, uio);
	if(res < 0) {
		return res;
	}

	done += res;
	res = uiomove((char *)name, namelen + 1, uio);
	if(res < 0) {
		return res;
	}

	done += res;
	assert(done <= dent->d_reclen);
	return uiomemset(uio, dent->d_reclen - done, 0x00);
}

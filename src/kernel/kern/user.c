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
#include <kern/fault.h>
#include <kern/atomic.h>
#include <vm/malloc.h>
#include <vm/layout.h>
#include <sys/limits.h>
#include <lib/string.h>
#include <arch/kwp.h>

int user_io_check(const void *buf, size_t size, size_t *maxsize) {
	if(!VM_REGION_IS_USER((uintptr_t)buf, size)) {
		return -EFAULT;
	}

	if(size == 0 && maxsize) {
		*maxsize = USER_VM_END - (uintptr_t)buf + 1;
	}

	return 0;
}

static int rubyte(char *buf, const char *u) {
	mayfault(error) {
		*buf = *u;
	}

	return 0;

error:
	return -EFAULT;
}

int copyin(void *kbuf, const void *ubuf, size_t size) {
	int err;

	err = user_io_check(ubuf, size, NULL);
	if(err) {
		return err;
	}

	mayfault(error) {
		memcpy(kbuf, ubuf, size);
	}

	return 0;

error:
	return -EFAULT;
}

int copyout(void *ubuf, const void *kbuf, size_t size) {
	int err;

	err = user_io_check(ubuf, size, NULL);
	if(err) {
		return err;
	}

	/*
	 * If the kernel would be allowed to write to read-only
	 * memory, no fault would happen when userspace provides
	 * a pointer to read-only memory.
	 */
	assert(kwp_enabled());
	mayfault(error) {
		memcpy(ubuf, kbuf, size);
	}

	return 0;

error:
	return -EFAULT;
}

int umemset(void *buf, int c, size_t len) {
	int err;

	err = user_io_check(buf, len, NULL);
	if(err) {
		return err;
	}

	assert(kwp_enabled());
	mayfault(error) {
		memset(buf, c, len);
	}

	return 0;

error:
	return -EFAULT;
}

int copyinstr(char *buf, const char *str, size_t bufsz, size_t *out) {
	size_t maxlen, len = 0;
	int err;

	assert(bufsz > 0);

	/*
	 * First check if the user pointer is invalid.
	 */
	err = user_io_check(str, 0, &maxlen);
	if(err) {
		return err;
	}

	maxlen = min(bufsz, maxlen);
	while(maxlen) {
		/*
		 * Read one byte from the user.
		 */
		err = rubyte(buf, str);
		if(err) {
			return err;
		} else if(*buf == '\0') {
			/*
			 * Found the string terminator.
			 s*/
			if(out) {
				*out = len;
			}

			return 0;
		} else {
			/*
			 * Goto the next byte.
			 */
			buf++;
			str++;
			maxlen--;
			len++;
		}
	}

	if(bufsz <= len) {
		return -ENAMETOOLONG;
	} else {
		return -EFAULT;
	}
}

int copyin_path(const char *ustr, char **out) {
	char *buf;
	int err;

	buf = kmalloc(PATH_MAX, VM_NOFLAG);
	if(buf == NULL) {
		err = -ENOMEM;
	} else {
		err = copyinstr(buf, ustr, PATH_MAX, NULL);
		if(err) {
			kfree(buf);
		} else {
			*out = buf;
		}
	}

	return err;
}

int copyin_atomic(void *buf, const void *ubuf, size_t size) {
	int err;

	err = user_io_check(ubuf, size, NULL);
	if(err) {
		return err;
	}

	mayfault(error) {
		atomic_loadn(buf, ubuf, size);
	}

	return 0;

error:
	return -EFAULT;
}

int copyout_atomic(void *ubuf, const void *buf, size_t size) {
	int err;

	err = user_io_check(ubuf, size, NULL);
	if(err) {
		return err;
	}

	mayfault(error) {
		atomic_storen(ubuf, buf, size);
	}

	return 0;

error:
	return -EFAULT;
}

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
#include <kern/init.h>
#include <kern/random.h>
#include <vfs/dev.h>
#include <vfs/file.h>
#include <sys/stat.h>
#include <lib/string.h>

static ssize_t null_read(__unused struct file *file, __unused uio_t *uio) {
	return 0;
}

static ssize_t null_write(__unused struct file *file, __unused uio_t *uio) {
	return -ENOSPC;
}

static ssize_t zero_read(__unused struct file *file, uio_t *uio) {
	ssize_t size = uio->size;
	int err;

	err = uiomemset(uio, uio->size, 0x00);
	if(err) {
		size = err;
	}

	return size;
}

static ssize_t random_read(__unused struct file *file, uio_t *uio) {
	size_t read = 0;
	char buf[512];

	while(uio->size) {
		size_t i, num = min(sizeof(buf), uio->size);
		ssize_t moved;

		for(i = 0; i < num; i++) {
			buf[i] = krand();
		}

		moved = uiomove(buf, num, uio);
		if(moved < 0) {
			return moved;
		}

		read += moved;
	}

	return read;
}

static ssize_t random_write(__unused struct file *file, uio_t *uio) {
	return uio->size;
}

static int mem_open(__unused struct file *file) {
	return 0;
}

static fops_t null_ops = {
	.open = mem_open,
	.read = null_read,
	.write = null_write,
};

static fops_t zero_ops = {
	.open = mem_open,
	.read = zero_read,
	.write = null_write,
};

static fops_t random_ops = {
	.open = mem_open,
	.read = random_read,
	.write = random_write,
};

static int __init mem_init(void) {
	int err;

	err = makechar(NULL, MAJOR_MEM, 0666, &null_ops, NULL, NULL, "null");
	if(err) {
		return INIT_PANIC;
	}

	err = makechar(NULL, MAJOR_MEM, 0666, &zero_ops, NULL, NULL, "zero");
	if(err) {
		return INIT_PANIC;
	}

	err = makechar(NULL, MAJOR_MEM, 0666, &random_ops, NULL, NULL,
		"random");
	if(err) {
		return INIT_PANIC;
	}

	err = makechar(NULL, MAJOR_MEM, 0666, &random_ops, NULL, NULL,
		"urandom");
	if(err) {
		return INIT_PANIC;
	}

	return INIT_OK;
}

fs_initcall(mem_init);

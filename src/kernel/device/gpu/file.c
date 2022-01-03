/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 *
 * Copyright (c) 2018, Elias Zell
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
#include <vfs/file.h>
#include <vfs/dev.h>
#include <device/gpu/gpu.h>
#include <device/gpu/private.h>

#if 1
static fop_open_t gpu_fop_open;
static fop_close_t gpu_fop_close;
static fop_ioctl_t gpu_fop_ioctl;
static fops_t gpu_fops = {
	.open = gpu_fop_open,
	.close = gpu_fop_close,
	.ioctl = gpu_fop_ioctl,
};

static int gpu_fop_open(file_t *file) {
	gpu_device_t *device = file_get_priv(file);
#if 0
	int err;

	if((file->flags & O_EXCL) || file_writeable(file)) {
		return -EINVAL;
	}

	synchronized(&device->lock) {
		if(device->open_count++ == 0) {
			err = gpu_init(device);
			if(err) {
				return err;
			}
		}
	}
#endif
	(void) device;
	return 0;
}

static void gpu_fop_close(file_t *file) {
	(void) file;
}

static int gpu_fop_ioctl(file_t *file, int cmd, void *arg) {
	(void) file;
	(void) cmd;
	(void) arg;
	return 0;
}

int gpu_file_create(gpu_device_t *device) {
	return makechar(NULL, MAJOR_GPU, 0444, &gpu_fops, device, &device->file,
		"gpu%d", gpu_id(device));
}

void gpu_file_destroy(gpu_device_t *device) {
	destroydev(device->file);
}
#endif

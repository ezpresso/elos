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
#include <kern/sync.h>
#include <device/gpu/gpu.h>
#include <device/gpu/private.h>

DEFINE_LIST(gpu_list);
DEFINE_SYNC(gpu_lock, MUTEX);

void gpu_init(gpu_device_t *gpu, gpu_driver_t *driver, void *priv) {
	list_node_init(gpu, &gpu->node);
	list_init(&gpu->crtc_list);
	list_init(&gpu->connector_list);
	list_init(&gpu->encoder_list);
	gpu->driver = driver;
	gpu->priv = priv;
}

void gpu_uninit(gpu_device_t *gpu) {
	list_node_destroy(&gpu->node);
	list_destroy(&gpu->crtc_list);
	list_destroy(&gpu->connector_list);
	list_destroy(&gpu->encoder_list);
}

static void gpu_insert(gpu_device_t *gpu) {
	gpu_device_t *cur;
	int expected = 0;

	sync_assert(&gpu_lock);

	/*
	 * The gpu list is sorted by the gpu->id field in ascending order.
	 * This way it is easy to get the smallest not used gpu id.
	 */
	foreach(cur, &gpu_list) {
		if(cur->id != expected) {
			gpu->id = expected;
			list_insert_before(&gpu_list, &cur->node, &gpu->node);
			return;
		} else {
			expected++;
		}
	}

	gpu->id = expected;
	list_append(&gpu_list, &gpu->node);
}

static void gpu_remove(gpu_device_t *gpu) {
	list_remove(&gpu_list, &gpu->node);
}

void gpu_register(gpu_device_t *gpu) {
	int err;

	sync_scope_acquire(&gpu_lock);
	gpu_insert(gpu);
	err = gpu_file_create(gpu);
	if(err) {
		kprintf("[gpu] warning: coud not create file for %d (%d)\n",
			gpu->id, err);
		gpu_remove(gpu);
	}
}

int gpu_unregister(gpu_device_t *gpu) {
	sync_scope_acquire(&gpu_lock);
	gpu_file_destroy(gpu);
	gpu_remove(gpu);
	return 0;
}

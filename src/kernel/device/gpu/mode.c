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
#include <device/gpu/gpu.h>
#include <device/gpu/mode.h>

static void gpu_add_mode_object(void *object, list_t *list, list_node_t *node,
	int *id)
{
	list_node_init(object, node);
	*id = list_length(list);
	list_append(list, node);
}

void gpu_crtc_init(gpu_device_t *device, gpu_crtc_ops_t *ops,
	gpu_crtc_t *crtc)
{
	crtc->device = device;
	crtc->ops = ops;
	gpu_add_mode_object(crtc, &device->crtc_list, &crtc->node, &crtc->id);
}

void gpu_encoder_init(gpu_device_t *device, gpu_encoder_type_t type,
	gpu_encoder_ops_t *ops, gpu_encoder_t *encoder)
{
	encoder->device = device;
	encoder->ops = ops;
	encoder->type = type;
	encoder->crtc_mask = 0;
	gpu_add_mode_object(encoder, &device->encoder_list, &encoder->node,
		&encoder->id);
}

void gpu_connector_init(gpu_device_t *device, gpu_connector_type_t type,
	gpu_connector_ops_t *ops, gpu_connector_t *connector)
{
	connector->device = device;
	connector->ops = ops;
	connector->type = type;
	for(size_t i = 0; i < GPU_CONNECTOR_MAX_ENCODER; i++) {
		connector->encoders[i] = NULL;
	}

	gpu_add_mode_object(connector, &device->connector_list,
		&connector->node, &connector->id);
}

void gpu_connector_add_encoder(gpu_connector_t *connector, gpu_encoder_t *enc) {
	for(size_t i = 0; i < GPU_CONNECTOR_MAX_ENCODER; i++) {
		if(connector->encoders[i] != NULL) {
			connector->encoders[i] = enc;
			return;
		}
	}

	kprintf("[gpu] warning: too many encoders for connector\n");
}

void gpu_encoder_add_crtc(gpu_encoder_t *encoder, gpu_crtc_t *crtc) {
	encoder->crtc_mask |= (1 << crtc->id);
}

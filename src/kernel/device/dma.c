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
#include <device/device.h>
#include <device/dma.h>
#include <device/_device.h>
#include <block/block.h>

static bus_size_t bus_dma_boundary(bus_size_t boundary,
	bus_size_t eng_boundary)
{
	if(boundary == 0 || eng_boundary == 0) {
		return max(boundary, eng_boundary);
	} else {
		return min(boundary, eng_boundary);
	}
}

static void bus_dma_eng_clone(bus_dma_engine_t *dst, bus_dma_engine_t *src,
	bus_addr_t start, bus_addr_t end, bus_size_t maxsz, bus_size_t align,
	bus_size_t boundary)
{
	dst->start = max(start, src->start);
	dst->end = min(end, src->end);
	dst->align = max(align, src->align);
	dst->maxsz = min(maxsz, src->maxsz);
	dst->boundary = bus_dma_boundary(boundary, src->boundary);
}

void bus_dma_create_engine(device_t *bus, bus_addr_t start, bus_addr_t end,
	bus_size_t maxsz, bus_size_t align, bus_size_t boundary,
	bus_dma_ops_t *ops, bus_dma_engine_t *dma)
{
	if(bus->dma) {
		bus_dma_eng_clone(dma, bus->dma, start, end, maxsz, align,
			boundary);
	} else {
		dma->start = start;
		dma->end = end;
		dma->align = align;
		dma->boundary = boundary;
		dma->maxsz = maxsz;
	}

	dma->ops = ops;
	bus->dma = dma;
	ops->create(dma);
}

void bus_dma_destroy_engine(bus_dma_engine_t *eng) {
	eng->ops->destroy(eng);
}

void bus_dma_restrict(device_t *bus, bus_addr_t start, bus_addr_t end,
	bus_size_t maxsz, bus_size_t align, bus_size_t boundary,
	bus_dma_engine_t *dma)
{
	/* TODO maybe we need a dma->restrict op */
	bus_dma_eng_clone(dma, bus->dma, start, end, maxsz, align, boundary);
	dma->ops = bus->dma->ops;
	dma->priv = bus->dma->priv;
	bus->dma = dma;
}

void bus_dma_create_buf(device_t *device, bus_addr_t start, bus_addr_t end,
	bus_size_t align, bus_size_t boundary, bus_size_t nseg,
	bus_size_t segsz_max, int flags, bus_dma_buf_t *buf)
{
	bus_dma_engine_t *dma = device->dma;

	buf->dma = dma;
	buf->flags = flags;
	buf->start = max(start, dma->start);
	buf->end = min(end, dma->end);
	buf->boundary = bus_dma_boundary(boundary, dma->boundary);
	buf->align = max(align, dma->align);
	buf->nseg_max = nseg;
	buf->segsz_max = segsz_max;
	buf->segs = NULL;
	buf->nseg = 0;
	buf->map = NULL;
	buf->priv = NULL;
	dma->ops->create_buf(buf);
}

void bus_dma_destroy_buf(bus_dma_buf_t *buf) {
	buf->dma->ops->destroy_buf(buf);
}

int bus_dma_alloc_mem(bus_dma_buf_t *buf, size_t size) {
	if(size > buf->dma->maxsz || size > buf->nseg_max * buf->segsz_max) {
		return -E2BIG;
	}

	return buf->dma->ops->alloc_mem(buf, size);
}

void bus_dma_free_mem(bus_dma_buf_t *buf) {
	buf->dma->ops->free_mem(buf);
}

int bus_dma_load(bus_dma_buf_t *buf, void *ptr, size_t size) {
	if(size > buf->dma->maxsz || size > buf->nseg_max * buf->segsz_max) {
		return -E2BIG;
	}

	return buf->dma->ops->load(buf, ptr, size);
}

int bus_dma_load_blk(bus_dma_buf_t *buf, blk_req_t *req) {
	assert(req->type == BLK_RD || req->type == BLK_WR);

	if(req->flags & BLK_REQ_PHYS) {
		return buf->dma->ops->load_phys(buf, req->io.paddr,
			req->io.cnt << blk_get_blkshift(req->pr));
	} else if(req->io.map) {
		return buf->dma->ops->load(buf, req->io.map,
			req->io.cnt << blk_get_blkshift(req->pr));
	} /* else if(req->io.uio) {
		bus_dma_load_uio(buf, req);
	} */
	else {
		kpanic("[bus-dma] cannot handle the block request");
	}
}

void bus_dma_unload(bus_dma_buf_t *buf) {
	buf->dma->ops->unload(buf);
}

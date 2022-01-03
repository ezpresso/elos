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
#include <arch/barrier.h>
#include <vm/mmu.h> /* vtophys */
#include <vm/malloc.h>

/*
 * TODO actually implement bounce buffering...
 * This DMA engine implementation is just a "hopefully kmalloc returns a
 * correct buffer" engine.
 * However I don't use any hardware where this is a problem
 * (and a VM_32BIT malloc flag suffices once the kernel is ported to x86_64)
 */

static void bus_dma_bounce_create(bus_dma_engine_t *eng) {
	(void) eng;
}

static void bus_dma_bounce_destroy(bus_dma_engine_t *eng) {
	(void) eng;
}

static void bus_dma_bounce_create_buf(bus_dma_buf_t *buf) {
	(void) buf;
}

static void bus_dma_bounce_destroy_buf(bus_dma_buf_t *buf) {
	if(buf->segs) {
		kfree(buf->segs);
	}
}

static int bus_dma_bounce_alloc_mem(bus_dma_buf_t *buf, size_t size) {
	bus_size_t nseg, i, segsz;
	bus_dma_seg_t *segs;
	void *mem;

	segsz = min(PAGE_SZ, buf->segsz_max);

	assert(buf->align <= PAGE_SZ);
	assert(buf->boundary >= segsz);
	assert(buf->start == 0x0);
	assert(buf->end == BUS_ADDR_MAX);

	/*
	 * Calculate the number of segments.
	 */
	nseg = size / segsz;
	if(nseg > buf->nseg_max) {
		return -E2BIG;
	}

	segs = kmalloc(sizeof(bus_dma_seg_t) * nseg, VM_WAIT);
	mem = kmalloc(size, VM_WAIT);

	for(i = 0; i < nseg; i++) {
		vm_paddr_t addr = vtophys(mem + i * segsz);

		assert(addr > buf->start && addr < buf->end);
		assert(ALIGNED(addr, buf->align));

		segs[i].addr = addr;
		segs[i].size = max(segsz, size - i * segsz);
	}

	/*
	 * Initialize the buffer.
	 */
	buf->segs = segs;
	buf->nseg = nseg;
	buf->map = mem;

	return 0;
}

static void bus_dma_bounce_free_mem(bus_dma_buf_t *buf) {
	kfree(buf->map);
	kfree(buf->segs);
}

static int bus_dma_bounce_prep(bus_dma_buf_t *buf, uint64_t start,
	size_t size)
{
	bus_size_t nseg;

	assert(buf->segsz_max >= PAGE_SZ);

	/*
	 * Calculate the number of pages involved in the buffer.
	 * Is there an easier way to do this?
	 */
	nseg = atop(ALIGN(start + size, PAGE_SZ) - (start & PAGE_MASK));
	if(nseg > buf->nseg_max) {
		return -E2BIG;
	}

	/*
	 * Allocate space for the segments.
	 */
	if(buf->nseg != nseg) {
		if(buf->segs) {
			kfree(buf->segs);
		}

		/* TODO this allocation should not fail in case of pageout. */
		buf->segs = kmalloc(sizeof(bus_dma_seg_t) * nseg, VM_NOFLAG);
		if(!buf->segs) {
			return -ENOMEM;
		}

		buf->nseg = nseg;
	}

	return 0;
}

static int bus_dma_bounce_load(bus_dma_buf_t *buf, void *ptr, size_t size) {
	uintptr_t start = (uintptr_t) ptr;
	bus_size_t i;
	size_t off;
	int err;

	err = bus_dma_bounce_prep(buf, start, size);
	if(err) {
		return err;
	}

	off = start & PAGE_MASK;
	for(i = 0; i < buf->nseg; i++) {
		size_t len = min(PAGE_SZ - off, size);
		vm_paddr_t addr = vtophys(ptr);

		assert(addr >= buf->start && addr < buf->end);
		buf->segs[i].addr = addr;
		buf->segs[i].size = len;

		ptr += len;
		size -= len;
		off = 0;
	}

	buf->map = ptr;

	return 0;
}

static int bus_dma_load_phys(bus_dma_buf_t *buf, vm_paddr_t phys, size_t size) {
	bus_size_t i;
	size_t off;
	int err;

	err = bus_dma_bounce_prep(buf, phys, size);
	if(err) {
		return err;
	}

	off = phys & PAGE_MASK;
	for(i = 0; i < buf->nseg; i++) {
		size_t len = min(PAGE_SZ - off, size);

		assert(phys >= buf->start && phys < buf->end);
		buf->segs[i].addr = phys;
		buf->segs[i].size = len;

		phys += len;
		size -= len;
		off = 0;
	}

	return 0;
}

static void bus_dma_bounce_unload(bus_dma_buf_t *buf) {
	buf->map = NULL;
}

static void bus_dma_bounce_sync(bus_dma_buf_t *buf, bus_dma_sync_t sync) {
	(void) buf;

	/*
	 * TODO this is not needed by coherent mappings
	 */
	if(sync == BUS_DMA_SYNC_DEV) {
		wmb();
	} else {
		rmb();
	}
}

bus_dma_ops_t bus_dma_bounce_ops = {
	.create = bus_dma_bounce_create,
	.destroy = bus_dma_bounce_destroy,
	.create_buf = bus_dma_bounce_create_buf,
	.destroy_buf = bus_dma_bounce_destroy_buf,
	.alloc_mem = bus_dma_bounce_alloc_mem,
	.free_mem = bus_dma_bounce_free_mem,
	.load = bus_dma_bounce_load,
	.load_phys = bus_dma_load_phys,
	.unload = bus_dma_bounce_unload,
	.sync = bus_dma_bounce_sync,
};

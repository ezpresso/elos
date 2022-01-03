#ifndef DEVICE_DMA_H
#define DEVICE_DMA_H

struct bus_dma_buf;
struct bus_dma_engine;
struct blk_req;

typedef enum bus_dma_sync {
	BUS_DMA_SYNC_DEV, /* Make dma memory available to device */
	BUS_DMA_SYNC_CPU, /* Make dma memory available to cpu */
} bus_dma_sync_t;

typedef struct bus_dma_ops {
	void (*create)		(struct bus_dma_engine *eng);
	void (*destroy)		(struct bus_dma_engine *eng);
	void (*create_buf)	(struct bus_dma_buf *);
	void (*destroy_buf)	(struct bus_dma_buf *);
	int  (*alloc_mem)	(struct bus_dma_buf *, size_t);
	void (*free_mem)	(struct bus_dma_buf *);
	int  (*load)		(struct bus_dma_buf *, void *, size_t);
	int  (*load_phys)	(struct bus_dma_buf *, vm_paddr_t, size_t);
	void (*unload)		(struct bus_dma_buf *);
	void (*sync) 		(struct bus_dma_buf *, bus_dma_sync_t);
} bus_dma_ops_t;

typedef struct bus_dma_engine  {
	/* address restrictions */
	bus_size_t boundary;
	bus_size_t align;
	bus_addr_t start;
	bus_addr_t end;
	bus_size_t maxsz;

	/* dma engine ops */
	bus_dma_ops_t *ops;

	/* dma engine private pointer */
	void *priv;
} bus_dma_engine_t;

typedef struct bus_dma_seg {
	bus_addr_t addr; /* bus address */
	bus_size_t size;
} bus_dma_seg_t;

typedef struct bus_dma_buf {
	bus_dma_engine_t *dma;
	int flags;

	/* address restrictions */
	bus_size_t boundary;
	bus_size_t align;
	bus_addr_t start;
	bus_addr_t end;
	bus_size_t nseg_max;
	bus_size_t segsz_max;

	bus_dma_seg_t *segs;
	bus_size_t nseg;
	void *map;
	void *priv;
} bus_dma_buf_t;

extern bus_dma_ops_t bus_dma_bounce_ops;

#define foreach_dma_seg(buf, i, seg)		\
	for(i = 0, seg = &(buf)->segs[0];	\
		i < (buf)->nseg;		\
		seg = &(buf)->segs[++i])

/**
 * @brief Create a new dma engine.
 *
 * A dma engine is responsible for providing bus addresses for a dma buffer and
 * making sure that the memory written by the cpu is visible by the device and
 * vice versa. Every child device of the device creating the engine will
 * use this engine, except if those devices create their own one. An example
 * of a dma engine is the bounce-buffer dma engine and an iommu-based dma
 * engine. 
 */
void bus_dma_create_engine(device_t *bus, bus_addr_t start, bus_addr_t end,
	bus_size_t maxsz, bus_size_t align, bus_size_t boundary,
	bus_dma_ops_t *ops, bus_dma_engine_t *dma);

/**
 * This creates a new dma engine by restricting the address limitations of the
 * parent's dma engine. bus_dma_create_engine has to also be called when the
 * engine is no longer needed.
 */
void bus_dma_restrict(device_t *bus, bus_addr_t start, bus_addr_t end,
	bus_size_t maxsz, bus_size_t align, bus_size_t boundary,
	bus_dma_engine_t *dma);

/**
 * @brief Destroy a dma engine.
 * @see bus_dma_create_engine
 */
void bus_dma_destroy_engine(bus_dma_engine_t *eng);

/**
 * @brief Create a new dma buffer object.
 *
 * Creates a new dma buffer. This does not yet allocate memory for the buffer.
 *
 */
void bus_dma_create_buf(device_t *device, bus_addr_t start, bus_addr_t end,
	bus_size_t align, bus_size_t boundary, bus_size_t nseg,
	bus_size_t segsz_max, int flags, bus_dma_buf_t *buf);

/**
 * @brief Destroy a dma buffer.
 *
 * Memory associated with the dma buffer has to be freed or unloaded before
 * destroying the buffer.
 */
void bus_dma_destroy_buf(bus_dma_buf_t *buf);

int bus_dma_load(bus_dma_buf_t *buf, void *ptr, size_t size);
int bus_dma_load_blk(bus_dma_buf_t *buf, struct blk_req *req);

void bus_dma_unload(bus_dma_buf_t *buf);
int bus_dma_alloc_mem(bus_dma_buf_t *buf, size_t size);
void bus_dma_free_mem(bus_dma_buf_t *buf);

static inline void bus_dma_sync(bus_dma_buf_t *buf, bus_dma_sync_t sync) {
	buf->dma->ops->sync(buf, sync);
}

static inline void *bus_dma_get_map(bus_dma_buf_t *buf) {
	return buf->map;
}

static inline bus_addr_t bus_dma_addr(bus_dma_buf_t *buf) {
	assert(buf->nseg == 1);
	return buf->segs[0].addr;
}

static inline bus_size_t bus_dma_size(bus_dma_buf_t *buf) {
	assert(buf->nseg == 1);
	return buf->segs[0].size;
}

#endif
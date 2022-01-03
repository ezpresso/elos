#ifndef BLOCK_DEVICE_H
#define BLOCK_DEVICE_H

#include <lib/list.h>

struct blk_dev;
struct blk_req;

typedef int (*blk_dev_start_t) (struct blk_dev *, struct blk_req *);

/* TODO instead if BLK_DEV_MAPIO use BLK_FEAT_PHYIO */
#define BLK_DEV_MAPIO	(1 << 0) /* perform mapped I/O */
#define BLK_DEV_NOMEDIA	(1 << 1) /* no removable media inserted */

typedef struct blk_req_queue {
	sync_t lock;
	list_t pending;
	size_t avail;
	/* The number of parallel request that can be handled. */
	size_t maxreq;
	blk_event_t event;
	bool running;
} blk_req_queue_t;

/* A physical block device */
typedef struct blk_dev {
	const char *name;
	size_t unit;
	struct blk_object *obj;
	int flags;
	void *priv;

	/* TODO rename */
	blk_dev_start_t start; /* request callback */

	uint8_t blk_shift; /* log2(logical block size) */
	uint8_t pblk_shift; /* log2(physical block size) */
	blkcnt_t blkcnt; /* total number of logical blocks */

	blk_req_queue_t *req_queue;
} blk_dev_t;

static inline void *blk_dev_priv(blk_dev_t *dev) {
	return dev->priv;
}

blk_dev_t *blk_dev_alloc(void);

/**
 * @brief Register a new block device.
 */
void blk_dev_register(blk_dev_t *dev);

/**
 * @brief Unregister a block device.
 */
void blk_dev_unregister(blk_dev_t *dev);

void blk_media_gone(blk_dev_t *dev);

void blk_media_inserted(blk_dev_t *dev);

void blk_dev_req_done(blk_req_t *req, int err);

blk_req_queue_t *blk_req_queue_alloc(size_t maxreq);
void blk_req_queue_free(blk_req_queue_t *list);

#endif
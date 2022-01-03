#ifndef BLOCK_BLOCK_H
#define BLOCK_BLOCK_H

#include <kern/rwlock.h>
#include <kern/atomic.h>
#include <kern/sync.h>
#include <vm/flags.h>
#include <lib/list.h>

/*
 * Example:
 *
 * /dev/disk0 ------------------------------------------------------+
 *								    |
 * /dev/disk0s0 -> blk_provider("part0")			    |
 *				     +--> blk_object("PART") --> blk_dev
 * /dev/disk0s1 -> blk_provider("part1")
 */

#define BLK_MAXDEPTH 	4
#define BLK_MAXNAME	10

struct blk_req;
struct file;
struct blk_provider;
struct blk_object;

typedef void (*blk_callback_t) (void *);

typedef struct blk_event {
	list_node_t node;
	blk_callback_t callback;
	void *arg;
} blk_event_t;

typedef int blk_request_t (struct blk_provider *, struct blk_req *);
/* TODO create will have to be rethought. */
typedef int blk_create_t (struct blk_provider *);
typedef void blk_destroy_t (struct blk_object *);
typedef void blk_prov_lost_t (struct blk_object *, struct blk_provider *);

typedef struct blk_class {
	const char	*name;
	blk_request_t	*request;
	blk_create_t	*create;
	blk_destroy_t	*destroy;
	blk_prov_lost_t *prov_lost;
} blk_class_t;

typedef struct blk_object {
	char name[BLK_MAXNAME];
	list_t consuming; /**< The list of providers this object uses */
	list_t providers; /**< The list of providers this object exports */
	blk_class_t *class;
	size_t depth;
	void *priv;
	uint8_t blk_shift; /**< The logical block size */
	uint8_t pblk_shift; /**< The physical block size (pblk_shift >= blk_shift) */
} blk_object_t;

typedef struct blk_provider {
	char name[BLK_MAXNAME]; /**< The name in the /dev directory */
	blk_object_t *obj;
	list_node_t node;

	/**
	 * The object using this provider. This field might be NULL even
	 * if this provider is used. To check whether an object is used
	 * use the BLK_P_INUSE.
	 */
	blk_object_t *user;
	list_node_t user_node;

#define BLK_P_RO 	(1 << 0)
#define BLK_P_RMV	(1 << 1) /* No further requests for this provider */
#define BLK_P_INUSE	(1 << 2) /* This provider is being used by someone */
#define BLK_P_DEVFS	(1 << 3) /* This provider has a devfile */
	int flags;
	void *priv;
	struct blk_cache *cache;
	ref_t ref;
	dev_t dev;
} blk_provider_t;

/*
 * Block request type
 */
typedef enum blk_rtype {
	BLK_RD,
	BLK_WR,
	/* BLK_EJECT for CD/DVD */
	/* BLK_SYNC to flush cache */
} blk_rtype_t;

/*
 * A block request handler capable of handling the completion of
 * one or more requests.
 */
typedef struct blk_handler {
	sync_t lock;
	blk_event_t event;
	int err;

#define BLK_HAND_SETUP	(1 << 0)
#define BLK_HAND_ASYNC	(1 << 1)
	int flags;
	size_t num;
	size_t done;
} blk_handler_t;

#define BLK_REQ_AUTOFREE (1 << 0)
#define BLK_REQ_PHYS	(1 << 1)

typedef struct blk_req {
	/*
	 * This node is used for the request list (see block/device.h).
	 */
	list_node_t node;

	blk_handler_t *handler;
	blk_provider_t *pr;
	blk_rtype_t type;
	int flags;

	union {
		/*
		 * Arguments for BLK_RD / BLK_WR.
		 */
		struct {
			blkno_t blk;
			blkcnt_t cnt;
			void *map; /* For mapped I/O */
			vm_paddr_t paddr; /* physical address */
		} io;
	};
} blk_req_t;

extern rwlock_t blk_lock;

static inline void *blk_object_priv(blk_object_t *obj) {
	return obj->priv;
}

static inline void *blk_provider_priv(blk_provider_t *pr) {
	return pr->priv;
}

static inline blk_provider_t *blk_provider_ref(blk_provider_t *pr) {
	ref_inc(&pr->ref);
	return pr;
}

void blk_provider_unref(blk_provider_t *pr);

/* block subsys internals */
blk_object_t *blk_object_new(blk_class_t *class, void *priv);
void blk_object_destroy(blk_object_t *obj);
void blk_object_destroy_pr(blk_object_t *obj);
blk_provider_t *blk_provider_new(blk_object_t *obj, void *priv);
void blk_provider_destroy(blk_provider_t *pr);
int blk_use_provider(blk_provider_t *pr, blk_object_t *obj);
void blk_disuse_provider(blk_provider_t *pr);

/**
 * @brief Called when a request is finished.
 *
 * Device drivers should call blk_dev_req_done instead.
 */
void blk_req_done(blk_req_t *req, int err);

/**
 * @brief Called by kern_mount in vfs/fs.c.
 */
int blk_mount_get(struct file *file, blk_provider_t **prp);

int blk_file_new(blk_provider_t *pr);
void blk_file_destroy(blk_provider_t *pr);

/**
 * @brief Called when unmounting a filesystem.
 */
void blk_mount_put(blk_provider_t *pr);

void blk_req_init(blk_req_t *req, blk_provider_t *pr, blk_handler_t *hand,
	blk_rtype_t type, int flags);
void blk_req_uninit(blk_req_t *req);

blk_req_t *blk_req_new(blk_provider_t *pr, blk_handler_t *handler,
	blk_rtype_t type, int flags, vm_flags_t alloc_flags);
void blk_req_free(blk_req_t *req);

blk_handler_t *blk_handler_new(int flags);
void blk_handler_free(blk_handler_t *hand);
int blk_handler_start(blk_handler_t *hand);
void blk_abort(blk_handler_t *hand);

int blk_req_launch(blk_req_t *req);

/**
 * Perform cached I/O on a block device, which is not restricted to sector
 * aligned access. This type of I/O is usually used for reading/writing
 * filesystem structures. However the contents of a file are cached by
 * using the vm-object of a vnode. Writes are immediately written to disk,
 * to not break any key filesystem structures after a system failure.
 */
int bio(blk_provider_t *pr, blk_rtype_t type, off_t off, size_t size,
	void *ptr);
#define bread(pr, off, sz, ptr)  bio(pr, BLK_RD, off, sz, ptr)
#define bwrite(pr, off, sz, ptr) bio(pr, BLK_WR, off, sz, ptr)

/*
 * TODO remember that the data is always cached in the physical-sector size
 * of the device (not not necessarily the legacy 512-byte sectors).
 */
typedef struct blkbuf {
	struct blk_cbuf *cbuf;
	void *buffer;
} blkbuf_t;
/* TODO this is how I/O will be done in the future. */
int blk_getbuf(blk_provider_t *pr, size_t size, off_t off, blkbuf_t *buf);
void blk_putbuf(blkbuf_t *);

/**
 * @brief Perform I/O on a block device.
 *
 * Reads / writes @p cnt number of blocks starting at @p no into the
 * buffer @p ptr.
 *
 * @param pr	The provider structure (returned by blk_mount_get). Filesystem
 *				drivers can use fs->dev for this
 * @param type 	The type of I/O: either BLK_RD or BLK_WR.
 * @param no	The first block that should be read/written.
 * @param cnt 	The number of blocks to be written / read.
 * @param ptr	A pointer to a memory region, which is either the
 *				destination (reads) or the source (writes) of
 *				the I/O.
 *
 * @return 		0 on success
 *				-EIO on error
 */
int blk_io(blk_provider_t *pr, blk_rtype_t type, blkno_t no, blkcnt_t cnt,
	void *ptr);
#define blk_read(pr, no, cnt, ptr) blk_io(pr, BLK_RD, no, cnt, ptr)
#define blk_write(pr, no, cnt, ptr) blk_io(pr, BLK_WR, no, cnt, ptr)

#if 0
/**
 * @brief Perform I/O on a block device.
 *
 * Reads / writes @p cnt number of blocks starting at @p no into the
 * the physical memory @p addr. The number of bytes read / written
 * have to fit into the rest of the page of @p addr.
 *
 * @param pr	The provider structure (returned by blk_mount_get). Filesystem
 *		drivers can use fs->dev for this
 * @param type 	The type of I/O: either BLK_RD or BLK_WR
 * @param no	The first black that should be read/written
 * @param cnt 	The number of blocks to be written / read
 * @param addr	A address of physical memory (max 1 page)
 * @param map	If the page page is mapped into the virtual address space of the
 *		kernel, its address may be specified here and it might be used
 *		for some optimizations (i.e. the if device driver needs the
 *		buffer to be mapped in kernel virtual space). If it is not
 *		mapped, this arg will be NULL.
 *
 * @return 		0 on success
 *				-EIO on error
 */
int blk_phyio(blk_provider_t *pr, blk_rtype_t type, blkno_t no, blkcnt_t cnt,
		vm_paddr_t addr, void *map);
#endif

static inline blksize_t blk_get_blksize(blk_provider_t *pr) {
	return 1 << pr->obj->blk_shift;
}

static inline blksize_t blk_get_blkshift(blk_provider_t *pr) {
	return pr->obj->blk_shift;
}

static inline blkno_t blk_off_to_blk(blk_provider_t *pr, uint64_t off) {
	return off >> pr->obj->blk_shift;
}

static inline dev_t blk_provider_dev(blk_provider_t *pr) {
	assert(F_ISSET(pr->flags, BLK_P_INUSE));
	assert(F_ISSET(pr->flags, BLK_P_DEVFS));
	return pr->dev;
}

void blk_event_create(blk_event_t *event, blk_callback_t callback, void *arg);
void blk_event_destroy(blk_event_t *event);
blk_event_t *blk_event_alloc(blk_callback_t callback, void *arg);
void blk_event_free(blk_event_t *event);
void blk_event_add(blk_event_t *event);

/**
 * @brief Initialize the block subsystem.
 */
void init_block(void);

#endif

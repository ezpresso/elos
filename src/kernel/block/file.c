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
#include <block/block.h>
#include <vfs/file.h>
#include <vfs/uio.h>
#include <vfs/dev.h>
#include <sys/stat.h>

static int blk_open(file_t *file);

static fops_t blk_dev_fops = {
	.open = blk_open,
	/* DO NOT ALLOW UNALIGNED ACCESS */
};

static int blk_open(file_t *file) {
	if(F_ISSET(file->flags, O_APPEND | O_TRUNC)) {
		return -EINVAL;
	}

	return 0;
}

#if 0
static ssize_t blk_read(file_t *file, uio_t *uio) {
	blk_provider_t *pr = file_get_priv(file);

	rdlock_scope(&blk_lock);
	if(pr->flags & BLK_P_UNUSE) {
		return -EBUSY;
	}
}
#endif

int blk_mount_get(file_t *file, blk_provider_t **prp) {
	blk_provider_t *pr;
	int err;

	if(file->type != FDEV) {
		return -ENOTBLK;
	}

	wrlock_scope(&blk_lock);

	/*
	 * Check if the file belongs to a block provider. This also checks
	 * whether the provider is still using this file (i.e. blk_file_destroy
	 * was not yet called)
	 */
	if(!dev_match_fops(file, &blk_dev_fops)) {
		return -ENOTBLK;
	}

	pr = file_get_priv(file);
	err = blk_use_provider(pr, NULL);
	if(err) {
		return err;
	}

	/*
	 * filesystems may want to use bio().
	 */
	extern void blk_cache_add(blk_provider_t *pr);
	blk_cache_add(pr);

	*prp = pr;
	return 0;
}

void blk_mount_put(blk_provider_t *pr) {
	wrlock_scope(&blk_lock);

	extern void blk_cache_rem(blk_provider_t *pr);
	blk_cache_rem(pr);
	blk_disuse_provider(pr);
}

int blk_file_new(blk_provider_t *pr) {
	int mode, err;

	rwlock_assert(&blk_lock, RWLOCK_WR);
	assert(!F_ISSET(pr->flags, BLK_P_DEVFS));

	/*
	 * Calculate the mode.
	 */
	mode = 0400; /* Read only */
	if(!(pr->flags & BLK_P_RO)) {
		mode |= 0200;
	}

	err = makeblk(NULL, MAJOR_DISK, mode, blk_get_blkshift(pr),
		&blk_dev_fops, pr, &pr->dev, "%s", pr->name);
	if(err == 0) {
		F_SET(pr->flags, BLK_P_DEVFS);
	}

	return err;
}

void blk_file_destroy(blk_provider_t *pr) {
	rwlock_assert(&blk_lock, RWLOCK_WR);
	assert(pr->flags & BLK_P_DEVFS);
	destroydev(pr->dev);
}

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
 * purpose with or without fee is hereby granted, proided that the above
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
#include <vfs/vnode.h>
#include <vfs/fs.h>
#include <vfs/uio.h>
#include <vm/object.h>
#include <vm/page.h>
#include <vm/phys.h>
#include <vm/pageout.h>
#include <block/block.h>

static void vop_page_zero(vnode_t *node, vm_page_t *page, vm_objoff_t off) {
	if((node->size & PAGE_MASK) == off) {
		size_t pgoff = node->size & ~PAGE_MASK;
		vm_page_zero_range(page, pgoff, PAGE_SZ - pgoff);
	} else {
		assert(off + PAGE_SZ <= node->size);
	}
}

static int vop_generic_page_rdwr(vnode_t *node, vm_page_t *page,
	vm_objoff_t off, blk_rtype_t io)
{
	blk_provider_t *dev = filesys_dev(node->fs);
	const blksize_t dev_blksz = blk_get_blksize(dev);
	const blksize_t blksz = vnode_blksz(node);
	blk_handler_t *handler;
	vm_paddr_t phys;
	int err;

	assert(dev_blksz <= (blksize_t)PAGE_SZ);
	assert(dev_blksz <= blksz);
	VN_ASSERT_LOCK_VM(node);

	handler = blk_handler_new(0);
	if(io == BLK_WR) {
		vop_page_zero(node, page, off);
	}

	phys = vm_page_phys(page);
	for(size_t i = 0; i < PAGE_SZ && off + i < node->size; i += blksz) {
		vm_objoff_t cur_off = off + i;
		blkno_t pbn, lbn;
		blk_req_t *req;
		size_t boff;
		int err;

		lbn = cur_off >> node->blksz_shift;
		boff = cur_off & (blksz - 1);

		/*
		 * Get the physical filesystem block from the logical offset.
		 */
		err = vnode_bmap(node, io == BLK_WR, lbn, &pbn);
		if(err) {
			blk_abort(handler);
			blk_handler_free(handler);
			return err;
		}

		if(pbn == 0) {
			assert(io == BLK_RD);
			vm_page_zero_range(page, i, blksz);
			continue;
		}

		/*
		 * Allocate a new request.
		 */
		req = blk_req_new(dev, handler, io,
			BLK_REQ_PHYS | BLK_REQ_AUTOFREE, 0 /* TODO */);

		/*
		 * Convert the filesystem block to device blocks. Remember
		 * that the device block size has to be smaller or equal to
		 * the filesystem block size. Thus there should not be an
		 * offset into the device block.
		 */
		req->io.blk = blk_off_to_blk(dev,
			(pbn << node->blksz_shift) + boff);

		/*
		 * Calculate the number of device blocks that are read or
		 * written.
		 */
		req->io.cnt = 1 << (node->blksz_shift - blk_get_blkshift(dev));

		/*
		 * Give the device the memory address.
		 */
		req->io.map = NULL;
		req->io.paddr = phys + i;

		/*
		 * Launch the requst. This does not wait for the request to
		 * finish, which is good for block devices allowing parallel
		 * requests. Furthermore the requests are automatically freed
		 * on error.
		 */
		err = blk_req_launch(req);
		if(err) {
			blk_abort(handler);
			blk_handler_free(handler);
			return err;
		}
	}

	/*
	 * TODO asynchronous writes
	 */
	err = blk_handler_start(handler);
	/* TODO not for async */
	blk_handler_free(handler);

	if(err) {
		return err;
	}

	if(io == BLK_RD) {
		vop_page_zero(node, page, off);
	} else {
		/*
		 * Try syncing the vnode (the bmap-callback might have
		 * allocated a block and thus the node might be dirty).
		 * TODO error value?
		 */
		vnode_sync(node);
	}

	return 0;
}

int vop_generic_pagein(vnode_t *node, vm_objoff_t off, vm_page_t *page) {
	/*
	 * TODO the lock is held during this.
	 * If we'd move to an asynchronous read, we could unlock the
	 * vm_object/vnode
	 */
	int err = vop_generic_page_rdwr(node, page, off, BLK_RD);
	if(err) {
		return -EIO;
	} else {
		return 0;
	}
}

ssize_t vop_generic_rdwr(vnode_t *node, uio_t *uio) {
	vm_flags_t access = uio->rw == UIO_WR ? VM_PROT_WR : VM_PROT_RD;
	size_t size, pgoff, done = 0;
	vm_page_t *page;
	ssize_t res = 0;
	int err;

	/*
	 * TODO
	 * There is a massive problem, because the user memory might
	 * need to be paged in and if the paged in memory is memory
	 * from the vnode itself, we got a deadlock.
	 *
	 * Update: I'm not sure if it would deadlock since a pagefault
	 * only locks node->object.lock and it this lock is not
	 * locked while doing the copyin/out
	 */
#if notyet
	uio_pin_pages(uio);
	lock(node);
	dorw(uio);
	unlock(node);
	uio_unpin_pages(uio)
#endif

	if(uio->rw == UIO_WR) {
		VN_ASSERT_LOCK_WR(node);
	} else {
		VN_ASSERT_LOCK_RD(node);
	}

	while(uio->size) {
		pgoff = uio->off & ~PAGE_MASK;
		size = min(PAGE_SZ - pgoff, uio->size);

		/*
		 * This is needed here, because otherwise vnode_getpage could
		 * return -ERANGE (aka EOF) on write.
		 */
		if((vnode_size_t)uio->off + size > node->size) {
			assert(uio->rw == UIO_WR);
			vnode_set_size(node, uio->off + size);
		}

		/*
		 * TODO when writing, vnode_getpage does not need to
		 * initialize the whole page, when a cache miss happens.
		 * This could be extremely useful, because libc buffers file
		 * I/O and hopefully writes larger blocks at once.
		 */
		synchronized(&VNTOVM(node)->lock) {
			err = vnode_getpage(node, uio->off & PAGE_MASK,
				access, &page);
		}
		if(err) {
			return err;
		}

		res = uiomove_page(page, size, pgoff, uio);
		vm_page_unpin(page);
		if(res < 0) {
			break;
		}

		done += res;
	}

	if(done > 0) {
		wrlock_scope(&node->statlock);
		vnode_settime(node, VN_CURTIME,
			uio->rw == UIO_WR ? VN_MTIME : VN_ATIME);
		vnode_dirty(node);
	}

	if(res < 0 || (res = vnode_sync(node)) < 0) {
		return res;
	} else {
		return done;
	}
}

bool vop_generic_set_exe(vnode_t *node) {
	return !!(vnode_flags_set(node, VN_EXE) & VN_EXE);
}

void vop_generic_unset_exe(vnode_t *node) {
	vnode_flags_t flags;

	flags = vnode_flags_clear(node, VN_EXE);
	assert(F_ISSET(flags, VN_EXE));
}

int vop_generic_pageout(vnode_t *node, vm_page_t *page) {
	int err;

	err = vop_generic_page_rdwr(node, page, vm_page_offset(page), BLK_WR);
	if(err == 0) {
		vm_page_clean(page);
		node->dirty--;
	}

	return err;
}

void __noreturn vop_panic(vnode_t *node) {
	kpanic("[vnode] called invalid vnode op for filesystem %s "
		"(ino: %lld)\n", filesys_name(node->fs), node->ino);
}

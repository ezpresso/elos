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
#include <vm/malloc.h>
#include <vfs/vfs.h>
#include <vfs/file.h>
#include <vfs/fs.h>
#include <block/block.h>

int filesys_mount(const char *source, const char *fstype, int flags,
	const char *data, filesys_t **out)
{
	blk_provider_t *dev = NULL;
	fs_driver_t *driver;
	filesys_t *fs;
	int err;

	(void) data;

	driver = driver_get(fstype, DRIVER_FILESYS);
	if(driver == NULL) {
		return -EINVAL;
	}

	if(F_ISSET(driver->flags, FSDRV_SINGLETON)) {
		/*
		 * The mountlock is held all the time so no further
		 * synchronization is needed.
		 */
		if(driver->fs) {
			return *out = filesys_ref(driver->fs), 0;
		}
	} else if(F_ISSET(driver->flags, FSDRV_SOURCE)) {
		file_t *file;

		if(source == NULL) {
			driver_put(driver);
			return -EINVAL;
		}

		err = kern_open(source, O_RDONLY, 0, &file);
		if(err) {
			driver_put(driver);
			return err;
		}

		/*
		 * Ask the block interface for the underlying
		 * device or partition or whatever.
		 */
		err = blk_mount_get(file, &dev);
		file_unref(file);
		if(err) {
			driver_put(driver);
			return err;
		}
	}

	/*
	 * Create a new fs structure.
	 */
	fs = kmalloc(sizeof(*fs), VM_WAIT);
	ref_init(&fs->ref);
	sync_init(&fs->lock, SYNC_MUTEX);
	fs->driver = driver;
	fs->priv = NULL;
	fs->dev = dev;
	fs->flags = flags;

	err = driver->mount(fs, &fs->root);
	if(err) {
		kprintf("[fs] error while mounting a(n) %s filesystem\n",
			driver->name);
		if(F_ISSET(driver->flags, FSDRV_SOURCE)) {
			blk_mount_put(dev);
		}

		sync_destroy(&fs->lock);
		kfree(fs);
		driver_put(driver);
		fs = NULL;
	} else if(F_ISSET(driver->flags, FSDRV_SINGLETON)) {
		driver->fs = fs;
	}

	return *out = fs, err;
}

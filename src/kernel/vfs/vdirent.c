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
#include <kern/init.h>
#include <vm/malloc.h>
#include <vfs/vdirent.h>
#include <vfs/fs.h>
#include <vfs/vcache.h>
#include <lib/string.h>
#include <vm/slab.h>

static DEFINE_VM_SLAB(vdirent_cache, sizeof(vdirent_t), 0);

vdirent_t *vdirent_alloc(filesys_t *fs, const char *name, size_t length,
	ino_t owner, ino_t ino, int flags, vm_flags_t allocflags)
{
	vdirent_t *dirent;
	char *name_ptr;

	assert(!F_ISSET(flags, ~VD_PERM));

	dirent = vm_slab_alloc(&vdirent_cache, allocflags);
	if(unlikely(dirent == NULL)) {
		return NULL;
	}

	/*
	 * Small names are stored directly in the structure.
	 */
	if(length >= VDNAME_INLINE) {
		/*
		 * Allocate extra space for big names.
		 */
		dirent->bigname = kmalloc(length + 1, VM_NOFLAG);
		if(unlikely(dirent->bigname == NULL)) {
			vm_slab_free(&vdirent_cache, dirent);
			return NULL;
		}

		name_ptr = dirent->bigname;
	} else {
		name_ptr = dirent->name;
	}

	memcpy(name_ptr, name, length);
	name_ptr[length] = '\0';

	list_node_init(dirent, &dirent->node);
	list_node_init(dirent, &dirent->lru_node);
	dirent->fs = filesys_ref(fs);
	dirent->owner = owner;
	dirent->namelen = length;
	dirent->ino = ino;
	dirent->flags = flags;

	return dirent;
}

void vdirent_free(vdirent_t *dirent) {
	filesys_unref(dirent->fs);

	if(dirent->namelen >= VDNAME_INLINE) {
		kfree(dirent->bigname);
	}

	list_node_destroy(&dirent->node);
	list_node_destroy(&dirent->lru_node);
	vm_slab_free(&vdirent_cache, dirent);
}

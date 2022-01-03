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
#include <vfs/vmount.h>
#include <vfs/fs.h>

vmount_t *vmount_alloc(ino_t mountpoint, int flags) {
	vmount_t *mnt;

	mnt = kmalloc(sizeof(*mnt), VM_WAIT);
	rwlock_init(&mnt->lock);
	ref_init(&mnt->ref);
	list_node_init(mnt, &mnt->node);
	list_init(&mnt->children);

	mnt->mountpoint = mountpoint;
	mnt->flags = flags;
	mnt->root = 0;
	mnt->filesys = NULL;
	mnt->parent = NULL;

	return mnt;
}

void vmount_free(vmount_t *mount) {
	assert(mount->filesys == NULL);
	assert(mount->parent == NULL);

	rwlock_destroy(&mount->lock);
	list_node_destroy(&mount->node);
	list_destroy(&mount->children);

	kfree(mount);
}

void vmount_insert(vmount_t *parent, vmount_t *mount, struct filesys *fs,
	ino_t root)
{
	mount->filesys = filesys_ref(fs);
	mount->root = root;

	if(parent) {
		mount->parent = vmount_ref(parent);
		wrlock_scope(&parent->lock);
		list_append(&parent->children, &mount->node);
	}
}

vmount_t *vmount_lookup(vmount_t *parent, ino_t ino) {
	vmount_t *child;

	rdlock_scope(&parent->lock);
	foreach(child, &parent->children) {
		if(child->mountpoint == ino) {
			return vmount_ref(child);
		}
	}

	return NULL;
}

bool vmount_mountpoint_p(vmount_t *parent, ino_t ino) {
	vmount_t *child;

	rdlock_scope(&parent->lock);
	foreach(child, &parent->children) {
		if(child->mountpoint == ino) {
			return true;
		}
	}

	return false;
}

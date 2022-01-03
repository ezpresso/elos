# Kernel TODO

## VM

Make kern_text_start - kern_text_end readonly...

## CPU handling

Currently cpu->id is the x86 lapic id of the processor. This id however should be arch independent!

## Coding
This may cause problems:
```c
	sync_scope_acquire(&lock);
	/* ... */
	return 0;
error:
	/* ... */
	return err;
```

## VFS - device files

Sometimes we have multiple device files for a specific device type (e.g. /dev/disk0,/dev/disk1 or ...). Consider adding some generic mechanism for allocating these numbers (or use minor ids?)

## VFS - mountpoints

Disallow unlink / rename of a mountpoint.

## VFS - link (priority: low)

Implement the link syscall.

## VFS - proper symlink for ext2 (priority: low)

The symlink syscall does not work for larger symlinks (size > EXT2_SYMLINK_INLINE) in ext2.

## VFS - O_APPEND

F_SETF atomically sets O_APPEND which seems to be racy (see file_mmap)

## BLOCK - block caching size

Maybe consider to cache using a ```PAGE_SZ``` granularity.

## DEVICE - PS2

Redo the PS/2 driver (pressing a key during boot causes problems).

## DEVICE

- how to ensure that bus_priv is a pci_device?
- cleanup bus / device interface
- remove DEV_DEP_INTR

## Spinlock

Ticket based spinlock.

## Module / linker

Redo module / linker

## Sync

Add sync_assert_not_owned() and do not allow to sleep while HODLing a mtx-sync.

## Wait

The waitqueue interface uses the timer interface for implementing timeouts, which is probably not the best idea. Especially since we actually don't need any precision in this case. The timer interface uses one simple list and one spinlock internally, which may cause problems if too many entries are in the list.

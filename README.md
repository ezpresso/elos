# ELOS operating system

ELOS is a hobby project developed between 2016 and 2018 and mostly
consists of a monotlithic kernel. The kernel itself tries to mimic a
linux syscall interface and thus can be used together with musl-libc.

The rough design of the individual parts is heavily inspired by FreeBSD,
as I was learning everything from scratch. However, the implementation
is quite simplified and differs quite a bit.

Most parts of the kernel are still unfinished. The scheduler for example
should be redone and kernel logging is also a mess. Furthermore the kernel
probably has a gazillion bugs.

Features:
- x86 architechture
- Multicore
- Process / thread management
	- Signals
	- Process groups / sessions
	- Fork / clone
- Memory management
	- Buddy allocator for physical memory
	- Kernel slab allocator
	- Lazy physical memory allocation 
	- Copy-on-write for fork
	- Disk syncing of vnode-mmaps
	- Swap to disk for anon-mmaps is not implemented
- Device manager
- Virtual file system
- ext2 as physical file system
- Dynamically linked libc (musl) with elf
- Other featuers: tty, timekeep, etc.


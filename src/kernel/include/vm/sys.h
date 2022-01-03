#ifndef VM_SYS_H
#define VM_SYS_H

intptr_t sys_mmap2(void *addr, size_t length, int prot, int flags, int fd,
	unsigned long pgoffset);
int sys_munmap(uintptr_t addr, size_t length);
int sys_brk(uintptr_t addr);
int sys_mprotect(void *addr, size_t len, int prot);
int sys_madvise(uintptr_t addr, size_t length, int advice);

#endif
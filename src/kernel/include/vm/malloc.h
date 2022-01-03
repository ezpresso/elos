#ifndef VM_MALLOC_H
#define VM_MALLOC_H

#include <vm/flags.h>

void *kmalloc(size_t size, vm_flags_t flags);
void kfree(void *ptr);

void vm_malloc_init(void);

#endif
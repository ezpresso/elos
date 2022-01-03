# VM subsystem

## Race conditions

vm_object_resize - vm_fault() -- is there really one?

## Lazy page invalidation

Yeah, wanted to implement that for a while now...

## Things to consider before swap can be implemented
*vm-objects* need a pointer to a ```vm_pager_t```. This *vm_pager* (either swap-pager, vnode-pager or ... maybe xnu's memory compression?) is responsible for freeing pages without loosing the data (e.g. writing to disk). Then there would be a function called ```vm_object_get_page_resident()``` (yep, that's a long symbol indeed) which would look if the page of an object is already present and if it's not, try to page it in. If it's not in memory or in e.g. swapspace, the page is not considered resident and a page-fault would have allocate a new page which would be initialized by the new ```initpage``` callback of the object itself (every page of a vnode is considered resident!!!).
-- Mostly done

## Remove redundant sbrk-calls from libc

Musl-libc always tries the ```sbrk``` syscall before calling the ```mmap``` syscall whereas this kernel does not implement the ```sbrk``` syscall.

## Implement VM_RESERVED

The kernel might need to allocate some important memory when e.g. swapping pages to disk. Since swapping only happens when the memory pressure is high,
we might run into some problems, because the swapper needs memory for freeing memory when free memory is rare. To prevent a system deadlock in such a case, the swap code could allocate memory from a special reserved memory region.

## Shadow object

Remove ```vm_shadow_num_inc``` / ```vm_shadow_num_dec``` -- already done

This kernel tries to simplify shadow chains, once a shadow object gets destroyed and thus we have to maintain a list of child shadow objects. However we could live without the list and just with a counter, if we would try to simplify the shadow chains elsewhere like this:

```c
/* Old way of doing things */
void free_shadow(vm_object_t *object) {
	parent = object->parent;
	free(object);

	if (simplify) {
		only_child_remaining = list_get(&parent->children);
		only_child_remaining->parent = parent->grandparent;
		free(parent);
	}
}

/* New way of doing this */
void regularly_check(vm_object_t *object) {
	if (object->parent->num_children == 1) {
		// We're the only child!!!
		vm_object_t *gparent = parent->parent;
		free(object->parent);
		object->parent = gparent;
	}
}
```

## vmem

It would be nice to use the mman from the kernel vas for allocating kernel virtual memory, however this needs some more thought...

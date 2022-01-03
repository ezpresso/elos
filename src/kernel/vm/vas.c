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
#include <kern/cpu.h>
#include <vm/vas.h>
#include <vm/slab.h>
#include <vm/object.h>
#include <vm/vm.h>

static DEFINE_VM_SLAB(vm_map_slab, sizeof(vm_map_t), 0);

void vm_vas_debug(vm_vas_t *vas) {
	rdlock_scope(&vas->lock);
	kprintf("[vm] vas: 0x%x - 0x%x\n", vm_vas_start(vas), vm_vas_end(vas));
	kprintf("mappings:\n");
	mman_debug(&vas->mman);
	kprintf("[vm] vas: done\n");
}

static bool vm_vas_check_region(vm_vas_t *vas, vm_vaddr_t addr,
	vm_vsize_t size)
{
	return (addr >= vm_vas_start(vas) && addr <= vm_vas_end(vas) &&
		size - 1 <= vm_vas_end(vas) - addr);
}

static vm_map_t *vm_vas_map_lookup(vm_vas_t *vas, vm_vaddr_t addr) {
	mman_node_t *node;

	rwlock_assert(&vas->lock, RWLOCK_RD);
	node = mman_lookup(&vas->mman, addr);
	if(node) {
		return MMAN2VM(node);
	} else {
		return NULL;
	}
}

static vm_map_t *vm_vas_first_map(vm_vas_t *vas, vm_vaddr_t addr,
	vm_vsize_t size)
{
	mman_node_t *node;

	rwlock_assert(&vas->lock, RWLOCK_RD);
	node = mman_first_node(&vas->mman, addr, size);
	if(node) {
		return MMAN2VM(node);
	} else {
		return NULL;
	}
}

static inline vm_map_t *vm_map_alloc(vm_vas_t *vas, vm_flags_t flags,
	vm_flags_t max_prot, vm_vsize_t rsize, vm_object_t *object,
	vm_objoff_t off)
{
	vm_map_t *map;

	map = vm_slab_alloc(&vm_map_slab, VM_WAIT);
	list_node_init(map, &map->obj_node);
	mman_node_init(&map->node);
	sync_init(&map->lock, SYNC_MUTEX);
	map->vas = vas;
	map->flags = flags;
	map->max_prot = max_prot;
	map->real_size = rsize;
	map->object = vm_object_ref(object);
	map->offset = off;

	if(F_ISSET(flags, VM_MAP_SHADOW)) {
		vm_demand_shadow_register(map->object);
	}

	return map;
}

static inline void vm_map_free(vm_map_t *map) {
	if(F_ISSET(map->flags, VM_MAP_SHADOW)) {
		vm_demand_shadow_unregister(map->object);
	}

	vm_object_unref(map->object);
	list_node_destroy(&map->obj_node);
	mman_node_destroy(&map->node);
	sync_destroy(&map->lock);
	vm_slab_free(&vm_map_slab, map);
}

static void vm_vas_do_unmap(vm_vas_t *vas, vm_vaddr_t addr, vm_vsize_t size) {
	vm_vaddr_t end = vm_region_end(addr, size);
	vm_vaddr_t unmap_addr;
	vm_vsize_t unmap_size;
	vm_map_t *map;

	rwlock_assert(&vas->lock, RWLOCK_WR);
	map = vm_vas_first_map(vas, addr, size);

	while(map && vm_map_addr(map) < end) {
		/*
		 * The vm_map might be freed, so we have to ookup the
		 * next entry beforehand.
		 */
		vm_map_t *next = vm_map_next(map);

		sync_acquire(&map->lock);
		if(vm_map_addr(map) < addr) {
			unmap_addr = addr;
			unmap_size = vm_map_end(map) - addr + 1;

			/*
			 * If the mapping starts before the area to be
			 * unmapped, unmap the end of the mapping.
			 * In this case the region being freed might be
			 * completely inside the mapping and there might
			 * be one part at the beginning and one part at the
			 * end of the mapping that would still be valid. This
			 * would mean allocating another vm_map for the end
			 * part, because mappings are always contigous and
			 * one cannot make vm_maps with a hole. However it is
			 * considered undefined behaviour if the addr of
			 * munmap() is not an address previously obtained
			 * through mmap and thus we do not care about this
			 * case and simply free the complete rest of the
			 * mapping even if this means unmapping too much
			 * virtual memory.
			 */
			mman_node_free_tail(&vas->mman, &map->node,
				unmap_size);

			/*
			 * When the end of a mapping is unmapped the size
			 * of the mapping my not be unaligned anymore.
			 */
			map->real_size = vm_map_size(map);
			sync_release(&map->lock);
		} else if(vm_map_end(map) <= end) {
			/*
			 * Unmap the complete mapping.
			 */
			unmap_addr = vm_map_addr(map);
			unmap_size = vm_map_size(map);
			mman_free(&vas->mman, &map->node);
			sync_release(&map->lock);

			/*
			 * Remove the mapping from the object's map
			 * list after dropping map->lock, because
			 * othwerwise this deadlock could happen:
			 * pageout:		this function:
			 * 1. lock object 	1. lock map
			 * 2. lock map 		2. lock map->object
			 */
			vm_object_map_rem(map->object, map);
			vm_map_free(map);
		} else {
			/*
			 * Unmap the first part of the mapping.
			 */
			unmap_addr = vm_map_addr(map);
			unmap_size = end - vm_map_addr(map) + 1;
			map->real_size -= unmap_size;
			mman_node_free_head(&vas->mman, &map->node,
				unmap_size);
			sync_release(&map->lock);
		}

		vas->funcs->unmap(vas, unmap_addr, unmap_size);
		map = next;
	}
}

int vm_vas_map(vm_vas_t *vas, vm_vaddr_t addr, vm_vsize_t size,
	vm_object_t *object, vm_objoff_t off, vm_flags_t flags,
	vm_flags_t max_prot, void **out)
{
	vm_vsize_t realsz;
	vm_map_t *map;
	int err;

	kassert(ALIGNED(addr, PAGE_SZ), "[vm] vas: mapping at an unaligned "
		"address; 0x%x", addr);
	kassert(ALIGNED(off, PAGE_SZ), "[vm] vas: map: unaligned object "
		"offset: 0x%llx", off);
	kassert(size, "[vm] vas: mapping with zero length");
	kassert(!MMU_MAP_NO_WO || (flags & VM_PROT_RW) != VM_PROT_WR,
			"[vm] vas: writeonly mapping not supported");
	if(!F_ISSET(flags, VM_MAP_UNALIGNED)) {
		kassert(ALIGNED(size, PAGE_SZ), "[vm] vas: mapping with "
			"unaligned size: 0x%x", size);
	}
	VM_FLAGS_CHECK(flags, VM_PROT_RWX | VM_PROT_KERN | VM_PROT_USER |
		VM_MAP_SHARED | VM_MAP_FIXED | VM_MAP_PGOUT | VM_MAP_32 |
		VM_MAP_SHADOW | VM_MAP_UNALIGNED /* TODO ADD TO DOC */);
	VM_FLAGS_CHECK(max_prot, VM_PROT_RWX);

	realsz = size;
	size = ALIGN(size, PAGE_SZ);

	/*
	 * When mapping at a fixed address, the address has to be valited.
	 * Currently the address is ignored when mapping at a non fixed
	 * address.
	 */
	if(F_ISSET(flags, VM_MAP_FIXED) &&
		!vm_vas_check_region(vas, addr, size))
	{
		return -EINVAL;
	}

	map = vm_map_alloc(vas, flags, max_prot, realsz, object, off);
	wrlocked(&vas->lock) {
		if(F_ISSET(flags, VM_MAP_FIXED)) {
			err = vm_object_map(object, map);
			if(err) {
				break;
			}

			/*
			 * Unmap the region where the mapping has to
			 * be placed and insert the region into
			 * the vas. The kernel vas does not support
			 * fixed mappings, so this callback could
			 * technically be removed.
			 */
			vm_vas_do_unmap(vas, addr, size);
			vas->funcs->map_fixed(vas, addr, size, map);
		} else {
			/*
			 * Allocate a virtual address for the new mapping.
			 */
			err = vas->funcs->map(vas, size, map);
			if(err) {
				break;
			}

			err = vm_object_map(object, map);
			if(err) {
				mman_free(&vas->mman, &map->node);
				vas->funcs->unmap(vas, vm_map_addr(map),
					vm_map_end(map));
				break;
			}
		}

		if(out) {
			*out = (void *)vm_map_addr(map);
		}
	}

	if(err) {
		vm_map_free(map);
	}

	return err;
}

int vm_vas_unmap(vm_vas_t *vas, vm_vaddr_t addr, vm_vsize_t size) {
	kassert(ALIGNED(addr, PAGE_SZ) && ALIGNED(size, PAGE_SZ),
		"[vm] vas: unmap: invalid range: address: 0x%x size: 0x%x",
		addr, size);
	assert(size);

	if(!vm_vas_check_region(vas, addr, size)) {
		return -EINVAL;
	} else {
		wrlock_scope(&vas->lock);
		vm_vas_do_unmap(vas, addr, size);
		return 0;
	}
}

static void vm_vas_demand_shadow(vm_map_t *map) {
   	vm_object_t *old;

	extern bool vm_shadow_needed(vm_object_t *object);;

	sync_assert(&map->lock);
	if(!F_ISSET(map->flags, VM_MAP_SHADOW)) {
		return;
	}

	map->flags &= ~VM_MAP_SHADOW;
   	old = map->object;
	map->object = vm_demand_shadow(map->object, map->offset +
		map->real_size);

	/*
	 * The old object is not needed anymore if it was
	 * shadowed (i.e. drop the reference from map->object).
	 */
	sync_release(&map->lock);
	vm_object_unref(old);
	sync_acquire(&map->lock);
}

static vm_map_t *vm_vas_map_split(vm_vas_t *vas, vm_map_t *map,
	vm_vsize_t size)
{
	vm_vsize_t new_size;
	vm_map_t *new;

	assert(ALIGNED(size, PAGE_SZ));
	kassert(size < map->real_size, "[vm] vas: splitting map: invalid size: "
		"0x%x 0x%x", size, map->real_size);

	/*
	 * TODO I don't know if this is needed any longer.
	 */
	synchronized(&map->lock) {
		vm_vas_demand_shadow(map);
	}

	/*
	 * TODO: This is not safe cuz map->flags is not const without holding
	 * the lock.
	 */
	new = vm_map_alloc(vas, map->flags, map->max_prot,
		map->real_size - size, map->object, map->offset + size);

	new_size = vm_map_size(map) - size;
	mman_node_free_tail(&vas->mman, &map->node, new_size);
	map->real_size = size;

	mman_insert(&vas->mman, vm_map_addr(map) + size, new_size, &new->node);

	/*
	 * Inform the object about the new mapping.
	 */
	vm_object_map_add(new->object, new);

	return new;
}

int vm_vas_protect(vm_vas_t *vas, vm_vaddr_t addr, vm_vsize_t size,
	vm_flags_t flags)
{
	vm_map_t *map;

	VM_FLAGS_CHECK(flags, VM_PROT_RWX);
	kassert(ALIGNED(addr, PAGE_SZ) && ALIGNED(size, PAGE_SZ),
		"[vm] vas: protect: invalid range: address: 0x%x size: 0x%x",
		addr, size);
	assert(size);

	if(!vm_vas_check_region(vas, addr, size)) {
		return -EINVAL;
	}

	wrlock_scope(&vas->lock);
	map = vm_vas_map_lookup(vas, addr);
	if(map == NULL) {
		return 0;
	}

	/*
	 * The region being protected (currently) has to be inside one
	 * mapping.
	 */
	if(addr < vm_map_addr(map) || vm_map_end(map) - addr < size - 1) {
		return -EINVAL;
	}

	/*
	 * If the file being mmap'd was opened as read-only, don't allow
	 * write access here.
	 */
	if(F_ISSET(flags, ~map->max_prot)) {
		return -EACCES;
	} else if(flags == (map->flags & VM_PROT_RWX)) {
		/*
		 * Nothing has to be changed. map->flags can be read
		 * here without holding map->lock, because they only
		 * change while vas->lock is write-locked.
		 */
		return 0;
	}

	if(addr > vm_map_addr(map)) {
		map = vm_vas_map_split(vas, map, addr - vm_map_addr(map));
	}

	if(vm_map_end(map) - addr != size - 1) {
		vm_vas_map_split(vas, map, size);
	}

	synchronized(&map->lock) {
		vm_flags_t old = map->flags;

		map->flags = (map->flags & ~VM_PROT_RWX) | flags;

		/*
		 * Change the protection flags at hardware level.
		 * Upgrading the permissions will simply be done
		 * during page fault (TODO avoid those unnecessary
		 * faults, which are quite expensive for long shadow
		 * chains). Restricting the access needs an hw update
		 * (i.e. writeable to readonly).
		 */
		if((old & flags) != (old & VM_PROT_RWX)) {
			mmu_protect(&vas->mmu, addr, size,
				map->flags & VM_PROT_MASK);
		}
	}

	return 0;
}

int vm_vas_fault(vm_vas_t *vas, vm_vaddr_t addr, vm_flags_t access,
	vm_map_t **mapp, vm_object_t **objectp)
{
	vm_object_t *object;
	vm_map_t *map;

	if(addr < vm_vas_start(vas) || addr > vm_vas_end(vas)) {
		return -ENOENT;
	}

	rdlock(&vas->lock);

	/*
	 * Search the mapping via the rbtree.
	 */
	map = vm_vas_map_lookup(vas, addr);
	if(map == NULL) {
		rwunlock(&vas->lock);
		return -ENOENT;
	}

	object = map->object;

	/*
	 * The object has to be locked first, to adhere to the vm locking
	 * order. Pageout locks a vm-object and then it's mappings and if
	 * the map would be locked here first, a deadlock could happen.
	 */
	sync_acquire(&object->lock);
	sync_acquire(&map->lock);

	/*
	 * Check if the process has permissions to access the area.
	 */
	if(!vm_map_perm(map, access)) {
		sync_release(&object->lock);
		sync_release(&map->lock);
		rwunlock(&vas->lock);
		return -EPERM;
	}

	if(F_ISSET(map->flags, VM_MAP_SHADOW)) {
		/*
		 * This is a little bit ugly, because a new object needs to be
		 * locked and to comply with the locking rules, the map has
		 * to be released first in order to lock the object.
		 */
		sync_release(&object->lock);
		vm_vas_demand_shadow(map);
		sync_release(&map->lock);

		/*
		 * It is safe to read map->object without map->lock, because
		 * map->object only changes in vm_vas_demand_shadow() and this
		 * was just called above which means the shadow-object is
		 * already in place. Furthermore vas->lock is still readlocked
		 * and thus no fork might interfere.
		 */
		object = map->object;
		sync_acquire(&object->lock);
		sync_acquire(&map->lock);
	}

	/*
	 * Unlock the virtual address space, but return the map in a locked
	 * state to protect it.
	 */
	rwunlock(&vas->lock);

	return *mapp = map, *objectp = object, 0;
}

int vm_vas_lookup(vm_vas_t *vas, vm_vaddr_t addr, vm_map_t **mapp) {
	vm_map_t *map;

	if(addr < vm_vas_start(vas) || addr > vm_vas_end(vas)) {
		return -ENOENT;
	}

	rdlock(&vas->lock);

	/*
	 * Search the mapping via the rbtree.
	 */
	map = vm_vas_map_lookup(vas, addr);
	if(map == NULL) {
		rwunlock(&vas->lock);
		return -ENOENT;
	}

	sync_acquire(&map->lock);

	/*
	 * Shadow the object on demand.
	 */
	vm_vas_demand_shadow(map);

	/*
	 * Unlock the virtual address space, but return the map in a locked
	 * state to protect it.
	 */
	rwunlock(&vas->lock);
	return *mapp = map, 0;
}

void vm_vas_init(vm_vas_t *vas, vm_vaddr_t start, vm_vaddr_t end,
	vm_vas_funcs_t *funcs)
{
	vas->end = end;
	vas->funcs = funcs;
	rwlock_init(&vas->lock);
	mman_init(&vas->mman, start, end - start + 1);
	if(vas != &vm_kern_vas) {
		mmu_ctx_create(&vas->mmu);
	}
}

void vm_vas_destroy(vm_vas_t *vas) {
	mmu_ctx_destroy(&vas->mmu);
	rwlock_destroy(&vas->lock);
	mman_destroy(&vas->mman);
}

void vm_vas_switch(vm_vas_t *vas) {
	cpu_t *cpu = cur_cpu();

	/*
	 * TODO is a critical section needed? YES because of cur_cpu()!
	 * The only time this is called without being inside a critical
	 * section is from execve(). Think about it.
	 * This is not a problem for now, because threads currently don't
	 * change cpus.
	 */
	if(cpu->vm_vas != vas) {
		cpu->vm_vas = vas;
		mmu_ctx_switch(&vas->mmu);
	}
}

static void vm_map_fork(vm_vas_t *vas, vm_map_t *src) {
	vm_map_t *map;

	rwlock_assert(&src->vas->lock, RWLOCK_RD);
	sync_acquire(&src->lock);

	/*
	 * Do copy-on-write if src is not shared. However if the source object
	 * may not be written to, copy-on-write is useless.
	 */
	if(!F_ISSET(src->flags, VM_MAP_SHARED) && VM_PROT_WR_P(src->max_prot)) {
		if(!F_ISSET(src->flags, VM_MAP_SHADOW)) {
			/*
			 * Both, src and new become shadow objects of the old
			 * object now. The new mapping will inherit the SHADOW
			 * flag.
			 */
			 F_SET(src->flags, VM_MAP_SHADOW);
			 vm_demand_shadow_register(src->object);
		 }

		/*
		 * Clear the write permission in the hardware mapping to
		 * causing a page fault when attempting to write to the region
		 * being forked. The real memory copying happens happens during
		 * page faults.
		 */
		mmu_protect(&src->vas->mmu, vm_map_addr(src), vm_map_size(src),
			src->flags & (VM_PROT_EXEC | VM_PROT_RD));
	}

	map = vm_map_alloc(vas, src->flags, src->max_prot, src->real_size,
		src->object, src->offset);
	sync_release(&src->lock);

	mman_insert(&vas->mman, vm_map_addr(src), vm_map_size(src), &map->node);
	vm_object_map_add(map->object, map);
}

void vm_vas_fork(vm_vas_t *dst, vm_vas_t *src) {
	mman_node_t *node;

	wrlock_scope(&dst->lock);
	rdlock_scope(&src->lock);

	mman_foreach(node, &src->mman) {
		vm_map_fork(dst, MMAN2VM(node));
	}
}

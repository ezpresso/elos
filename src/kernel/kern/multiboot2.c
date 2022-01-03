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
#include <kern/multiboot.h>
#include <lib/string.h>
#include <vm/kern.h>
#include <vm/phys.h>

#define multiboot_foreach(cur) \
	for(cur = multiboot_tags; cur != NULL; cur = multiboot_next(cur))

#define __tag __align(MULTIBOOT_TAG_ALIGN)
#define __hdr __align(MULTIBOOT_HEADER_ALIGN)

static __used __section(".multiboot") struct {
	__hdr struct multiboot_header				header;
	__tag MULTIBOOT_HDR_TAG_INFO_REQ(BOOT_IREQ_NUM)		info;
	__tag struct multiboot_header_tag_entry_address		entry;
	__tag struct multiboot_header_tag_module_align		align;
	__tag struct multiboot_header_tag			end;
} header = {
	.header = {
		.magic = MULTIBOOT2_HEADER_MAGIC,
		.architecture = BOOT_ARCH,
		.header_length = sizeof(header),
		.checksum = -(MULTIBOOT2_HEADER_MAGIC + BOOT_ARCH +
			sizeof(header))
	},

	.info = {
		.type = MULTIBOOT_HEADER_TAG_INFORMATION_REQUEST,
		.flags = 0,
		.size = sizeof(struct multiboot_header_tag) +
			sizeof(uint32_t) * BOOT_IREQ_NUM,
		.requests = BOOT_IREQ,
	},

	.entry = {
		.type = MULTIBOOT_HEADER_TAG_ENTRY_ADDRESS,
		.flags = 0,
		.size = sizeof(struct multiboot_header_tag_entry_address),
		.entry_addr = BOOT_ENTRY,
	},

	.align = {
		.type = MULTIBOOT_HEADER_TAG_MODULE_ALIGN,
		.flags = 0,
		.size = sizeof(struct multiboot_header_tag_module_align),
	},

	.end = {
		.type = MULTIBOOT_HEADER_TAG_END,
		.flags = 0,
		.size = sizeof(struct multiboot_header_tag),
	}
};

static multiboot_tag_t *multiboot_tags, *multiboot_end;

/**
 * @brief Get the next multiboot tag.
 */
static multiboot_tag_t *multiboot_next(multiboot_tag_t *tag) {
	if(tag->tag.type == 0) {
		return NULL;
	}

	tag = (void *)tag + ALIGN(tag->tag.size, MULTIBOOT_TAG_ALIGN);
	if(tag >= multiboot_end) {
		return NULL;
	} else {
		return tag;
	}
}

multiboot_tag_t *multiboot_get_tag(uint32_t type) {
	multiboot_tag_t *cur;

	multiboot_foreach(cur) {
		if(cur->tag.type == type) {
			return cur;
		}
	}

	return NULL;
}

void *multiboot_acpi_rsdp(void) {
	multiboot_tag_t *tag;

	tag = multiboot_get_tag(MULTIBOOT_TAG_TYPE_ACPI_NEW);
	if(tag) {
		return (void *)tag->new_acpi.rsdp;
	}

	tag = multiboot_get_tag(MULTIBOOT_TAG_TYPE_ACPI_OLD);
	if(tag) {
		return (void *)tag->old_acpi.rsdp;
	}

	return NULL;
}

void *multiboot_module(const char *name, size_t *sizep) {
	multiboot_tag_t *cur;
	void *ptr;
	size_t size;
	int err;

	multiboot_foreach(cur) {
		if(cur->tag.type != MULTIBOOT_TAG_TYPE_MODULE ||
			strcmp(cur->module.cmdline, name))
		{
			continue;
		}

		size = cur->module.mod_end - cur->module.mod_start;
		if(sizep) {
			*sizep = size;
		}

		/*
		 * TODO make sure caller unmaps.
		 */

		/*
		 * VM_WAIT would not make sense here since this is called during
		 * early initialization and a failure is fatal.
		 */
		err = vm_kern_map_phys(cur->module.mod_start,
			ALIGN(size, PAGE_SZ), VM_PROT_RD, &ptr);
		if(err) {
			kpanic("[boot] could not map module: %s: 0x%x - 0x%x\n",
				cur->module.cmdline, cur->module.mod_start,
				cur->module.mod_end);
		}

		return ptr;
	}

	return NULL;
}

static void multiboot_parse_mmap(multiboot_tag_t *tag) {
	multiboot_mmap_t *cur, *end;
	vm_psize_t size;

	cur = tag->mmap.entries;
	end = (void *)tag + tag->mmap.size;
	while(cur < end) {
		/*
		 * Ignore non usable memory. Ignore any mamory above the
		 * architectures memory limit (e.g. x86 in 32bit mode with
		 * more than 4GB of ram).
		 */
		if(cur->type == MULTIBOOT_MEMORY_AVAILABLE &&
			cur->addr < VM_PHYS_MAX)
		{
			size = min((VM_PHYS_MAX - cur->addr) + 1, cur->len)
				& PAGE_MASK;
			if(size) {
				/*
				 * Register the memory in the phyiscal memory
				 * manager.
				 */
				vm_physeg_add(cur->addr, size);
			}
		}

		cur = (void *)cur + tag->mmap.entry_size;
	}
}

int multiboot_init_mem(void) {
	multiboot_tag_t *cur;
	size_t size;
	int mmap = 0;

	multiboot_foreach(cur) {
		if(cur->tag.type == MULTIBOOT_TAG_TYPE_MODULE) {
			assert(ALIGNED(cur->module.mod_start, PAGE_SZ));

			/*
			 * Reserve the memory of the module.
			 */
			size = cur->module.mod_end - cur->module.mod_start;
			vm_phys_reserve(cur->module.mod_start,
				ALIGN(size, PAGE_SZ), cur->module.cmdline);
		} else if(cur->tag.type == MULTIBOOT_TAG_TYPE_MMAP) {
			multiboot_parse_mmap(cur);
			mmap = 1;
		}
	}

	/*
	 * If the memory map information was not found, the kernel knows
	 * nothing about the physical memory.
	 */
	if(!mmap) {
		return -1;
	} else {
		return 0;
	}
}

void multiboot_init(vm_paddr_t addr) {
	multiboot_fixed_t *fixed;
	size_t offset, size;
	void *map;

	offset = addr & ~PAGE_MASK;
	map = vm_kern_map_phys_early(addr & PAGE_MASK, PAGE_SZ);

	fixed = map + offset;
	size = fixed->total_size;

	if(!INSIDE_PAGE(fixed, size)) {
		vm_kern_unmap_phys_early(map, PAGE_SZ);

		fixed = vm_kern_map_phys_early(addr & PAGE_MASK,
			ALIGN(offset + size, PAGE_SZ)) + offset;
	}

	multiboot_tags = (void *)&fixed[1];
	multiboot_end = (void *)fixed + size;

	vm_phys_reserve(addr & PAGE_MASK, ALIGN(size + offset, PAGE_SZ),
		"multiboot tags");
}

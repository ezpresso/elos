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
#include <kern/user.h>
#include <vm/phys.h>
#include <lib/string.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>

int sys_sysinfo(struct sysinfo *usr_info) {
	struct sysinfo info;

	memset(&info, 0x00, sizeof(struct sysinfo));
	info.mem_unit = PAGE_SZ;
	info.totalram = atop(vm_phys_get_total());
	info.freeram = atop(vm_phys_get_free());

	return copyout(usr_info, &info, sizeof(info));
}

int sys_uname(struct utsname *buf) {
	/* TODO */
	struct utsname tmp = {
		.sysname = "ELOS kernel",
		.nodename = "elos",
		.version = "0.1",
		.release = "ELOS kernel Version 0.1",
		.machine = "x86",
	};

	return copyout(buf, &tmp, sizeof(tmp));
}

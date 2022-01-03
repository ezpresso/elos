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
#include <kern/proc.h>
#include <kern/user.h>
#include <sys/resource.h>

#include <lib/string.h>

int sys_prlimit64(pid_t pid, int resource, const struct rlimit *new_limit,
	struct rlimit *old_limit)
{
	(void) pid;
	(void) new_limit;
	(void) old_limit;
	(void) resource;
	return 0;
}

int sys_getrusage(int who, struct rusage *usage) {
	/*
	 * TODO
	 */
	struct rusage ru;
	memset(&ru, 0x00, sizeof(ru));

	(void) who;
	return copyout(usage, &ru, sizeof(ru));
}

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
#include <kern/symbol.h>
#include <lib/string.h>

#undef strcat
#undef strncat

char *strcat(char *dst, const char *src) {
	char *old = dst;

	while(*dst) {
		dst++;
	}

	while((*dst++ = *src++)) {
		continue;
	}

	return old;
}

char *strncat(char *dst, const char *src, size_t max) {
	char *old = dst;

	kassert(max != 0, "[string] strncat: \"max\" is zero");

	/*
	 * Jump to the end of the first string.
	 */
	while(*dst++) {
		continue;
	}

	do {
		if((*dst = *src++) == '\0') {
			break;
		}

		dst++;
	} while(--max != 0);
	
	/*
	 * strncpy does not zero terminate the destination in some
	 * cases, but strncat does... The standard is very frustrating
	 * indeed.
	 */
	*dst = '\0';
	return old;
}

export(strncat);
export(strcat);
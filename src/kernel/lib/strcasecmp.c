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

int strcasecmp(const char *str1, const char *str2) {
	int c1, c2;

	do {
		c1 = tolower(*str1++);
 	   	c2 = tolower(*str2++);
	} while(c1 == c2 && c1 != '\0');

	return c1 - c2;
}

int strncasecmp(const char *str1, const char *str2, size_t max) {
	uint8_t c1, c2;

	kassert(max, "[string] strncasecmp: \"max\" is zero");
	do {
		c1 = *str1++;
		c2 = *str2++;
		if(c1 == c2) {
			continue;
		}

		c1 = tolower(c1);
		c2 = tolower(c2);
		if(c1 != c2) {
			break;
		}
	} while(--max);

	return (int)c1 - (int)c2;
}

export(strcasecmp);
export(strncasecmp);
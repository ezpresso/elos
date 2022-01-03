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
 * purpose with or without fee is hereby granted, proided that the above
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
#include <kern/init.h>
#include <kern/exec.h>
#include <lib/string.h>
#include <sys/limits.h>

#define SCRIPT_MAGIC	"#!"
#define SCRIPT_NMAG	2

/**
 * @brief Skip the whitespace from a string.
 */
static void script_skip_blank(char **strp, char *max) {
	char *str = *strp;

	while(str < max && isblank(*str)) {
		str++;
	}

	*strp = str;
}

/**
 * @brief Try to execute a file starting with e.g. '#!/bin/sh'
 * TODO "Linux ignores the set-user-ID and set-group-ID bits on scripts."
 */
static int script_exec(exec_img_t *image) {
	char *str, *max, *interp, *interp_end, *opt, *opt_end;
	size_t size, interp_sz, opt_sz;

	str = image->header;
	max = str + PAGE_SZ;

	/*
	 * Scripts always start with "#!".
	 */
	if(memcmp(str, SCRIPT_MAGIC, SCRIPT_NMAG)) {
		return EXEC_NOMAG;
	}

	/*
	 * Don't allow an interpreter to be a shell script. This
	 * avoids infinite recursion (interpreter is same as image->node).
	 *
	 * TODO could allow e.g. 4 scripts.
	 */
	if(image->flags & EXEC_SCRIPT) {
		return -ENOEXEC;
	}

	image->flags |= EXEC_SCRIPT;

	/*
	 * Find the path of the interpreter.
	 */
	str += SCRIPT_NMAG;

	/*
	 * Skip the space between the #! and the name.
	 */
	script_skip_blank(&str, max);
	interp = str;

	/*
	 * Loop over the interp string.
	 */
	while(str < max && !isblank(*str) && *str != '\n') {
		str++;
	}
	interp_end = str;

	/*
	 * Sanity checking of the interpreter name.
	 */
	if(interp == interp_end) {
		return -ENOEXEC;
	} else if(interp_end - interp > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/*
	 * Find the optional arg.
	 */
	script_skip_blank(&str, max);
	opt = str;

	while(str < max && !isspace(*str)) {
		str++;
	}
	opt_end = str;

	/*
	 * The new args look like:
	 * argv[0] = interpreter
	 * argv[1] = optional arg
	 * argv[2] = filename
	 * argv[3] = original argv[1]
	 *
	 * 2 and 3 are already in the buffer, so the first two
	 * strings need to be added. Check if enough space is available
	 * for the this.
	 */
	interp_sz = interp_end - interp;
	opt_sz = opt_end - opt;

	size = interp_sz + 1;
	if(opt_sz) {
		size += opt_sz + 1;
	}

	if(image->strspace < size) {
		return -E2BIG;
	}

	/*
	 * Make some space at the beginning of the argument buffer.
	 */
	memmove(image->strmem + size, image->strmem, image->strptr -
		image->strmem);
	
	image->strptr += size;
	image->strspace -= size;
	image->argsize += size;
	image->env += size;

	/*
	 * Copy the interpreter to the buffer.
	 */
	str = image->strmem;
	memcpy(str, interp, interp_sz);
	str[interp_sz] = '\0';
	image->argc++;

	/*
	 * Copy the optional arg to the buffer.
	 */
	if(opt_sz) {
		str = image->strmem + interp_sz + 1;
		memcpy(str, opt, opt_sz);
		str[opt_sz] = '\0';
		image->argc++;
	}

	return exec_interp(image, interp, interp_sz);
}

static binfmt_t script_binfmt = {
	.name = "script",
	.exec = script_exec,
	.initaux = NULL,
};

static __init int script_init(void) {
	binfmt_register(&script_binfmt);
	return INIT_OK;
}

late_initcall(script_init);
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

#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

int main(int argc, char *const argv[]) {
	sigset_t mask;
	int err;

	(void) argc;
	(void) argv;

	/*
	 * Don't do anything if the init script was
	 * not launched by the kernel.
	 */
	if(getpid() != 1) {
		fprintf(stderr, "init not started by kernel...\n");
		exit(EXIT_FAILURE);
	}

	/*
	 * Startup the system.
	 */
	if(!fork()) {
		char *const exe_argv[] = {
			"/bin/teststartup",
			NULL,
		};

		execv(exe_argv[0], exe_argv);
		perror("init: exec startup");
		exit(EXIT_FAILURE);
	}

	/*
	 * Block every possible signal.
	 */
	sigfillset(&mask);
	err = sigprocmask(SIG_SETMASK, &mask, NULL);
	if(err) {
		/*
		 * Should never happen.
		 */
		perror("init: could not block signals");
		exit(EXIT_FAILURE);
	}

	/*
	 * Reap processes.
	 */
	for(;;) {
		wait(NULL);
	}
}

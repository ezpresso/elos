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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mount.h>
#include <assert.h>
#include <fcntl.h>
#include <pty.h>

static const char banner[] = {
	219,219,219,219,219,219,219,187,219,219,187,' ',' ',' ',' ',' ',' ',219,219,219,219,219,219,187,
	' ',219,219,219,219,219,219,219,187,'\n',
	219,219,201,205,205,205,205,188,219,219,186,' ',' ',' ',' ',' ',219,219,201,205,205,205,219,219,
	187,219,219,201,205,205,205,205,188,'\n',
	219,219,219,219,219,187,' ',' ',219,219,186,' ',' ',' ',' ',' ',219,219,186,' ',' ',' ',219,219,
	186,219,219,219,219,219,219,219,187,'\n',
	219,219,201,205,205,188,' ',' ',219,219,186,' ',' ',' ',' ',' ',219,219,186,' ',' ',' ',219,219,
	186,200,205,205,205,205,219,219,186,'\n',
	219,219,219,219,219,219,219,187,219,219,219,219,219,219,219,187,200,219,219,219,219,219,219,201,
	188,219,219,219,219,219,219,219,186,'\n',
	200,205,205,205,205,205,205,188,200,205,205,205,205,205,205,188,' ',200,205,205,205,205,205,188,
	' ',200,205,205,205,205,205,205,188,'\n'
};
static int pty_master;
static pthread_t display_thread;

void *display_thread_func(void *arg) {
	/*
	 * Cannot read use a bigger buffer here, because otherwhise CTRL+C
	 * on a command like 'yes' would not be instant (lots of 'y' would
	 * be still printed before ^C).
	 */
	char tmp;
	int res;

	(void) arg;

	while(1) {
		res = read(pty_master, &tmp, sizeof(tmp));
		if(res > 0) {
			write(STDOUT_FILENO, &tmp, res);
		}
	}
}

void keyboard_thread(void) {
	char buffer[10];
	int rd;

	for(;;) {
		rd = read(STDIN_FILENO, buffer, sizeof(buffer));
		if(rd > 0) {
			write(pty_master, buffer, rd);
		}
	}
}

void terminal(void) {
	int res;

	res = forkpty(&pty_master, NULL, NULL, NULL);
	if(res < 0) {
		perror("forkpty");
		exit(EXIT_FAILURE);
	} else if(!res) {
		tcsetpgrp(0, getpgrp());

		char *sh_argv[] = {
			"/System/bin/dash",
			NULL,
		};

		char *sh_envp[] = {
			"PATH=/System/bin",
			"SHELL=/System/bin/dash",
			"LOGNAME=root",
			NULL,
		};

		execvpe(sh_argv[0], sh_argv, sh_envp);
		perror("error while staring a shell");
		exit(EXIT_FAILURE);
	}

	if(pthread_create(&display_thread, NULL, display_thread_func, NULL)) {
		perror("pthread: display thread");
		exit(EXIT_FAILURE);
	}

	keyboard_thread();
}

int main(int argc, const char *argv[]) {
	int err;

	(void) argc;
	(void) argv;

	/*
	 * We need user input!! STDOUT and STDERR are opened by the kernel.
	 */
	err = open("/dev/atkbd", O_RDONLY); /* STDIN */
	assert(err == STDIN_FILENO);

	/*
	 * Print the ELOS banner.
	 */
	for(size_t i = 0; i < (sizeof(banner) / sizeof(banner[0])); i++) {
		putchar(banner[i] & 0xff);
	}
	fflush(stdout);

	/*
	 * Try mountint the ext2fs on disk1 on /mnt
	 */
	err = mount("/dev/disk0", "/mnt", "ext2", 0, NULL);
	if(err) {
		perror("error while mounting disk0");
		exit(EXIT_FAILURE);
	}

	/*
	 * Chroot into our root filesystem.
	 */
	if((err = chdir("/mnt")) || (err = chroot("/mnt"))) {
		perror("error while chroot to root-fs");
		exit(EXIT_FAILURE);
	}

	/*
	 * We need another /dev after chrooting
	 */
	err = mount(NULL, "/dev", "devfs", 0, NULL);
	if(err) {
		perror("error while mounting devfs");
		exit(EXIT_FAILURE);
	}

	terminal();
	return EXIT_SUCCESS;
}

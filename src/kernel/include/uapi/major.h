#ifndef UAPI_MAJOR_H
#define UAPI_MAJOR_H

#define MINOR_MAX 32

enum major {
	MAJOR_MEM = 0, /* /dev/null, /dev/zero, /dev/random etc. */
	MAJOR_DISK, /* dev/disk0 /dev/disk0s0 etc. */
	MAJOR_NET,
	MAJOR_GPU,
	MAJOR_INPUT,
	MAJOR_TTY, /* /dev/ptym, /dev/tty /dev/pts/0 /devpts1 etc */
	MAJOR_MISC,
	MAJOR_KERN,
	NMAJOR,
};

#endif

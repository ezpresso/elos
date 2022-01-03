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
#include <device/device.h>
#include <drivers/input/i8042.h>

/* Temporary */
#include <lib/ringbuf.h>
#include <sys/stat.h>
#include <vfs/dev.h>
#include <vfs/file.h>

typedef struct atkbd {
	bus_res_t intr;
	bus_res_t io; /* not the real io ports */

#define ATKBD_ALT	(1 << 0)
#define ATKBD_SHIFT	(1 << 1)
#define ATKBD_CAPS	(1 << 2)
#define ATKBD_CTRL	(1 << 3)
	int flags;

	/* Temporary */
	dev_t dev;
	ringbuf_t buffer;
} atkbd_t;

#define ATKBD_RELEASED	0x80

static const char scancodes[] = {
	0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0, 0, '\b',
	'\t', 'q', 'w', 'e', 'r', 't', 'z', 'u', 'i', 'o', 'p', 0 /* ü */, '+','\n',
	0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 0 /* ö */, 0 /* ä */, '<',
	0,'#', 'y', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '-', 0,
	0, 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char scancodes_upper[] = {
	0, 0, '!', '\"', 0, '$', '%', '&', '/', '(', ')', '=', '?', '`', 0,
	0, 'Q', 'W', 'E', 'R', 'T', 'Z', 'U', 'I', 'O', 'P', 0, '*', '\n',
	0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 0 , 0, '>',
	0, '\'', 'Y', 'X', 'C', 'V', 'B', 'N', 'M', ';', ':', '_', 0,
	0, 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char scancodes_alt[] = {
	0, 0, 0, 0, 0, 0, '[', ']', '|', '{', '}'
};

static int atkbd_open(file_t *file) {
	if(FWRITEABLE(file->flags)) {
		return -EACCES;
	} else {
		return 0;
	}
}

static ssize_t atkbd_read(file_t *f, uio_t *uio) {
	device_t *device = file_get_priv(f);
	atkbd_t *kbd = device_priv(device);
	return ringbuf_read_uio(&kbd->buffer, uio, 0);
}

static fops_t atkbd_ops = {
	.read = atkbd_read,
	.open = atkbd_open
};

static void atkbd_intr_thr(void *arg) {
	atkbd_t *kbd = arg;
	uint8_t data = 0, c = 0;

	data = bus_res_readb(&kbd->io, I8042_DATA);
	switch(data) {
	case 0:
		return;
	case 58:
		if(kbd->flags & ATKBD_CAPS) {
			kbd->flags &= ~ATKBD_CAPS;
		} else {
			kbd->flags |= ATKBD_CAPS;
		}
		return;
	case 56:
		kbd->flags |= ATKBD_ALT;
		return;
	case 184:
		kbd->flags &= ~ATKBD_ALT;
		return;
	case 42:
		kbd->flags |= ATKBD_SHIFT;
		return;
	case 170:
		kbd->flags &= ~ATKBD_SHIFT;
		return;
	case 29:
		kbd->flags |= ATKBD_CTRL;
		return;
	case 157:
		kbd->flags &= ~ATKBD_CTRL;
		return;
	}

	if(data & ATKBD_RELEASED) {
		return;
	}

	/* TODO temporary */
	if(kbd->flags & ATKBD_SHIFT && kbd->flags & ATKBD_ALT) {
		if(data == 8) {
			c = '\\';
		}
	} else if(kbd->flags & ATKBD_CTRL && data == 46) {
		c = 3; /* ^C */
	} else if(kbd->flags & ATKBD_CTRL && data == 21) {
		c = 26; /* ^Z */
	} else if(kbd->flags & ATKBD_CTRL && data == 32) {
		c = 4; /* ^D */
	} else if(kbd->flags & ATKBD_CTRL && data == 22) {
		c = 21; /* ^U */
	} else if(kbd->flags & ATKBD_SHIFT || kbd->flags & ATKBD_CAPS) {
		if(data < NELEM(scancodes_upper)) {
			c = scancodes_upper[data];
		}
	} else if(kbd->flags & ATKBD_ALT) {
		if(data < NELEM(scancodes_alt)) {
			c = scancodes_alt[data];
		}
	} else {
		if(data < NELEM(scancodes)) {
			c = scancodes[data];
		}
	}

	if(c) {
		ringbuf_write(&kbd->buffer, sizeof(c), &c, RB_NOBLOCK);
	}
}

static int atkbd_intr(void *arg, __unused int intr) {
	atkbd_t *kbd = arg;

	if(bus_res_readb(&kbd->io, I8042_DATA_PRESENT)) {
		return BUS_INTR_ITHR;
	} else {
		return BUS_INTR_STRAY;
	}
}

static int atkbd_probe(__unused device_t *device) {
	return DEVICE_PROBE_OK;
}

static int atkbd_attach(device_t *device) {
	atkbd_t *kbd = device_priv(device);
	int err;

	device_set_desc(device, "AT keyboard");
	kbd->flags = 0;

	err = ringbuf_alloc(&kbd->buffer, 64);
	if(err) {
		return err;
	}

	err = bus_alloc_res(device, BUS_RES_INTR, 0, RF_NONE, &kbd->intr);
	if(err) {
		goto err_intr;
	}

	/*
	 * Allocate emulated IO port.
	 */
	err = bus_alloc_res(device, BUS_RES_IO, 0, RF_MAP, &kbd->io);
	if(err) {
		goto err_io;	
	}

	err = bus_setup_intr(device, atkbd_intr, atkbd_intr_thr, kbd,
		&kbd->intr);
	if(err) {
		goto err_map;
	}

	err = makechar(NULL, MAJOR_INPUT, 0400, &atkbd_ops, device, &kbd->dev,
		"atkbd");
	if(err) {
		goto err_file;
	}

	return 0;

err_file:
err_map:
	bus_free_res(device, &kbd->io);
err_io:
	bus_free_res(device, &kbd->intr);
err_intr:
	ringbuf_free(&kbd->buffer);

	kpanic("PANIC");
	return err;
}

static int atkbd_detach(device_t *device) {
	atkbd_t *kbd = device_priv(device);

	ringbuf_eof(&kbd->buffer);
	destroydev(kbd->dev);

	bus_free_res(device, &kbd->intr);
	bus_free_res(device, &kbd->io);
	ringbuf_free(&kbd->buffer);

	return 0;
}

static DEVTAB(atkbd_devtab, "atkbd", "ps2") {
	{ .id = 0x41AB },
	{ DEVTAB_END }
};

static DRIVER(atkbd_driver) {
	.name = "atkbd",
	.flags = DEV_DEP_INTR,
	.type = DEVICE_INPUT,
	.private = sizeof(atkbd_t),
	.devtab = &atkbd_devtab,
	.probe = atkbd_probe,
	.attach = atkbd_attach,
	.detach = atkbd_detach,
};

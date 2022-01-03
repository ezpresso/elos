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
#include <kern/log.h>
#include <kern/symbol.h>
#include <kern/init.h>
#include <kern/sync.h>
#include <kern/cpu.h>
#include <vfs/dev.h>
#include <lib/string.h>
#include <lib/ascii.h>

#define LOG_BUF 2048

static int pos_x = 0, pos_y = 0;
static bool log_screen_en = true;
static sync_t log_lock = SYNC_INIT(SPINLOCK);
static char log_buf[LOG_BUF];

static void log_putch(char c, log_color_t color) {
	log_on_put(c);

	switch(c) {
	case '\0':
		return;
	case '\r':
		pos_x = 0;
		break;
	case '\n':
		pos_x = 0;
		pos_y++;
		break;
	case '\t':
		pos_x = ALIGN(pos_x + 1, 8);
		break;
	case '\b':
		if(pos_x > 0) {
			pos_x--;
		} else if(pos_y > 0) {
			pos_y--;
			pos_x = log_width() - 1;
		}
		break;
	default:
		if(ASCII_CLASS(c) != ASCII_CONTROL) {
			log_putchar(pos_x, pos_y, color, c);
			pos_x++;
		}
		break;
	}

	if(pos_x >= log_width()) {
		pos_x = 0;
		pos_y++;
	}

	if(pos_y >= log_height()) {
		log_scroll();
		pos_y--;
	}

	log_set_cursor(pos_x, pos_y);
}

static void log_putstr(const char *c, log_color_t color) {
	while(*c != '\0') {
		log_putch(*c, color);
		c++;
	}
}

void log_panic(const char *c) {
	log_putstr(c, LOG_NORMAL);
}

void log_reset(void) {
	pos_x = 0;
	pos_y = 0;
	log_clear_screen();
	log_set_cursor(0, 0);
}
export(log_reset);

bool log_screen_enabled(void) {
	return log_screen_en;
}
export(log_screen_enabled);

void log_screen_disable(void) {
	log_screen_en = false;
}
export(log_screen_disable);

void log_screen_enable(void) {
	log_screen_en = true;
}
export(log_screen_enable);

int va_kprintf_color(log_color_t color, const char *fmt, va_list ap) {
	int res = 0;

	synchronized(&log_lock) {
		res = vsnprintf(log_buf, sizeof(log_buf), fmt, ap);
		log_putstr(log_buf, color);
	}

	return res;
}
export(va_kprintf_color);

int va_kprintf(const char *fmt, va_list arp) {
	return va_kprintf_color(LOG_NORMAL, fmt, arp);
}
export(va_kprintf);

int kprintf(const char *format, ...) {
	va_list ap;
	int ret;
	va_start(ap, format);
	ret = va_kprintf(format, ap);
	va_end(ap);
	return ret;
}
export(kprintf);

int kprintf_color(log_color_t color, const char *format, ...) {
	va_list ap;
	int ret;
	va_start(ap, format);
	ret = va_kprintf_color(color, format, ap);
	va_end(ap);
	return ret;
}
export(kprintf_color);

#include <vfs/file.h>
#include <vfs/uio.h>
#include <sys/stat.h>

static ssize_t log_write(__unused struct file *f, uio_t *uio) {
	size_t total = 0;
	char buf[128];

	while(uio->size) {
		ssize_t ret, i;

		ret = uiomove(buf, sizeof(buf), uio);
		if(ret < 0) {
			return ret;
		}

		total += ret;

		sync_scope_acquire(&log_lock);
		for(i = 0; i < ret; i++) {
			log_putch(buf[i] & 0xFF, LOG_NORMAL);
		}
	}


	return total;
}

int log_open(__unused struct file *f) {
	return 0;
}

static fops_t log_ops = {
	.write = log_write,
	.open = log_open,
};

static __init int log_init_fs(void) {
	int err;

	err = makechar(NULL, MAJOR_KERN, 0666, &log_ops, NULL, NULL,
		"console");
	if(err) {
		return INIT_ERR;
	}

	return INIT_OK;
}

fs_initcall(log_init_fs);
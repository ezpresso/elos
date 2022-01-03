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
#include <kern/init.h>
#include <lib/string.h>
#include <vm/layout.h>
#include <arch/x86.h>

/*
 * XXX Ugly hackish code to get something on screen.
 */

#define VGA_FB		0xB8000
#define VGA_W		80
#define VGA_H		25
#define VGA_D_BLUE	0x0
#define VGA_D_GREEN	0x1
#define VGA_YELLOW 	0xe
#define VGA_WHITE	0xf
#define VGA_GREY	0x7
#define VGA_GREEN	0xa
#define VGA_RED 	0xc
#define LFB_CHR_H	16
#define LFB_CHR_W	8
#define SE_PORT		0x3F8

static volatile uint8_t *vidmem = (volatile void *)(VGA_FB + KERNEL_VM_BASE);
static int width = VGA_W, height = VGA_H;
#if 0
static kgm_pixbuf_t *log_pixbuf;
#endif
extern uint8_t logging_8x16_font[4096];

static uint8_t log_vga_color(log_color_t color) {
	switch(color) {
	case LOG_RED:
		return VGA_RED;
	case LOG_YELLOW:
		return VGA_YELLOW;
	case LOG_GREY:
		return VGA_GREY;
	default:
		return VGA_WHITE;

	}
}

static void vga_set_cursor(int x, int y) {
	uint16_t loc = y * VGA_W + x;

	if(*(vidmem + loc * 2 + 1) == 0) {
		*(vidmem + loc * 2 + 1) = VGA_WHITE;
	}

	outb(0x3D4, 14);
	outb(0x3D5, loc >> 8);
	outb(0x3D4, 15);
	outb(0x3D5, loc);
}

static void vga_putchar(int x, int y, log_color_t color, char c) {
	uint16_t loc = y * VGA_W + x;

	*(vidmem + loc * 2) = c & 0xFF;
	*(vidmem + loc * 2 + 1) = log_vga_color(color);
}

static void vga_scroll(void) {
	uint16_t *ptr = (uint16_t *)vidmem;
	int i;

	/*
	 * buffers overlap -> memmove instead of memcpy
	 */
	memmove(ptr, ptr + VGA_W, (VGA_H - 1) * VGA_W * 2);
	for(i = (VGA_H - 1) * VGA_W; i < VGA_H * VGA_W; i++) {
		ptr[i] = 0x20;
	}
}

static void vga_clear_screen(void) {
	uint16_t *ptr = (uint16_t *)vidmem;

	for(int i = 0; i < VGA_H * VGA_W; i++) {
		ptr[i] = 0x00 | (VGA_WHITE << 8);
	}
}

#if 0
static void lfb_drawchar(int x, int y, log_color_t color, char c) {
	kgm_color_t black = { .r = 0x00, .g = 0x00, .b = 0x00 };
	kgm_color_t kgm_color = { .r = 0x00, .g = 0x00, .b = 0x00 };
	uint8_t *font = logging_8x16_font;
	int i, j;

	switch(color) {
	case LOG_RED:
		kgm_color.r = 0xFF;
		break;
	case LOG_YELLOW:
		kgm_color.r = 0xFF;
		kgm_color.g = 0xFF;
		break;
	case LOG_GREY:
		kgm_color.r = 0x80;
		kgm_color.g = 0x80;
		kgm_color.b = 0x80;
		break;
	default:
		kgm_color.r = 0xFF;
		kgm_color.g = 0xFF;
		kgm_color.b = 0xFF;
	}

	x *= LFB_CHR_W;
	y *= LFB_CHR_H;

	for(i = 0; i < LFB_CHR_H; i++) {
		for(j = 0; j < LFB_CHR_W; j++) {
			if(font[(c & 0xff) * LFB_CHR_H + i] & (1 << j)) {
				kgm_pixbuf_set(log_pixbuf, x+7-j, y+i, kgm_color);
			} else {
				kgm_pixbuf_set(log_pixbuf, x+7-j, y+i, black);
			}
		}
	}
}

/* The implementation of lfb_scroll is slow with high resolutions... */
static void lfb_scroll(void) {
	size_t pitch = log_pixbuf->pitch;
	size_t row_sz = log_pixbuf->w * log_pixbuf->bytespp;
	void *buf = log_pixbuf->buf;
	int h = log_pixbuf->h;
	int i;

	for(i = 0; i < (h - LFB_CHR_H); i++) {
		memcpy(buf + pitch * i, buf + (pitch * (i + LFB_CHR_H)), row_sz);
	}

	for(; i < h; i++) {
		memset(buf + pitch * i, 0, row_sz);
	}
}

static void lfb_clear_screen(void) {
	memset(log_pixbuf->buf, 0x00, log_pixbuf->pitch * log_pixbuf->h);
}
#endif

int log_width(void) {
	return width;
}

int log_height(void) {
	return height;
}

void log_set_cursor(int x, int y) {
	if(likely(log_screen_enabled())) {
#if 0
		if(!log_pixbuf)
#endif
		vga_set_cursor(x, y);
	}
}

void log_on_put(char c) {
	static bool port_init = false;

	if(c == '\n') {
		log_on_put('\r');
	}

	if(!port_init) {
		port_init = true;
		outb(SE_PORT + 1, 0x00);
		outb(SE_PORT + 3, 0x80);
		outb(SE_PORT + 0, 0x01);
		outb(SE_PORT + 1, 0x00);
		outb(SE_PORT + 3, 0x03);
		outb(SE_PORT + 2, 0xC7);
		outb(SE_PORT + 4, 0x0B);
		outb(SE_PORT + 1, 0x01);
	}

	while((inb(SE_PORT + 5) & 0x20) == 0);
	outb(SE_PORT, c);
}

void log_putchar(int x, int y, log_color_t color, char c) {
	if(likely(log_screen_enabled())) {
#if 0
		if(log_pixbuf) {
			lfb_drawchar(x, y, color, c);
		} else {
#endif
			vga_putchar(x, y, color, c);
#if 0
		}
#endif
	}
}

void log_scroll(void) {
	if(likely(log_screen_enabled())) {
#if 0
		if(log_pixbuf) {
			lfb_scroll();
		} else {
#endif
			vga_scroll();
#if 0
		}
#endif
	}
}

void log_clear_screen(void) {
	if(likely(log_screen_enabled())) {
#if 0
		if(log_pixbuf) {
			lfb_clear_screen();
		} else {
#endif
			vga_clear_screen();
#if 0
		}
#endif
	}
}

static __init int init_log(void) {
#if 0
	vbe_info_t *vbe_info = mboot_vbe_info();
	kgm_pixbuf_t *pixbuf;
	kgm_pix_format_t fmt;
	void *map;
	int res;

	if(!vbe_boot()) {
		return INIT_OK;
	}

	kpanic("[arch:log] cannot call phymap_map during early_initcall");

	fmt = kgm_color_shift_to_format(vbe_info->bpp, vbe_info->red_position,
				vbe_info->green_position, vbe_info->blue_position);
	if(fmt == KGM_FORMAT_UNK) {
		return INIT_ERR;
	}

	map = phymap_map(mboot_vbe_info()->physbase, vbe_info->Yres *
		vbe_info->pitch);
	if(!map) {
		return INIT_ERR;
	}

	pixbuf = kmalloc(sizeof(*pixbuf), VM_WAIT);
	if(!pixbuf) {
		return INIT_ERR;
	}

	res = kgm_pixbuf_init(pixbuf, map, vbe_info->Xres, vbe_info->Yres,
				vbe_info->bpp, vbe_info->pitch, fmt);
	if(res == KGM_ERR) {
		kfree(pixbuf);
		return INIT_ERR;
	}

	width = vbe_info->Xres / LFB_CHR_W;
	height = vbe_info->Yres / LFB_CHR_H;
	log_pixbuf = pixbuf;
	log_reset();
#endif

	return INIT_OK;
}

early_initcall(init_log);

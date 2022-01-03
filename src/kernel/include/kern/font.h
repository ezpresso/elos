#ifndef KERN_FONT_H
#define KERN_FONT_H

#include <kern/section.h>

#define KERN_FONTS section(kern_fonts, kern_font_t)

#define KERN_FONT(n) \
	/* static */ section_entry(KERN_FONTS) kern_font_t n = 

typedef struct kern_font {
	const char *name;
	size_t width;
	size_t height;
	const void *bitset;
} kern_font_t;

kern_font_t *kern_font_get(size_t xres, size_t yres);

#endif
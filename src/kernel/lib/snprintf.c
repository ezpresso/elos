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

/**************************************************************
 * Original:
 * Patrick Powell Tue Apr 11 09:48:21 PDT 1995
 * A bombproof version of doprnt (dopr) included.
 * Sigh.  This sort of thing is always nasty do deal with.  Note that
 * the version here does not include floating point...
 *
 * snprintf() is used instead of sprintf() as it does limit checks
 * for string length.  This covers a nasty loophole.
 *
 * The other functions are there to prevent NULL pointers from
 * causing nast effects.
 *
 * More Recently:
 *  Brandon Long <blong@fiction.net> 9/15/96 for mutt 0.43
 *  This was ugly.  It is still ugly.  I opted out of floating point
 *  numbers, but the formatter understands just about everything
 *  from the normal C string format, at least as far as I can tell from
 *  the Solaris 2.5 printf(3S) man page.
 *
 *  Brandon Long <blong@fiction.net> 10/22/97 for mutt 0.87.1
 *    Ok, added some minimal floating point support, which means this
 *    probably requires libm on most operating systems.  Don't yet
 *    support the exponent (e,E) and sigfig (g,G).  Also, fmtint()
 *    was pretty badly broken, it just wasn't being exercised in ways
 *    which showed it, so that's been fixed.  Also, formated the code
 *    to mutt conventions, and removed dead code left over from the
 *    original.  Also, there is now a builtin-test, just compile with:
 *           gcc -DTEST_SNPRINTF -o snprintf snprintf.c -lm
 *    and run snprintf for results.
 * 
 *  Thomas Roessler <roessler@guug.de> 01/27/98 for mutt 0.89i
 *    The PGP code was using unsigned hexadecimal formats. 
 *    Unfortunately, unsigned formats simply didn't work.
 *
 *  Michael Elkins <me@cs.hmc.edu> 03/05/98 for mutt 0.90.8
 *    The original code assumed that both snprintf() and vsnprintf() were
 *    missing.  Some systems only have snprintf() but not vsnprintf(), so
 *    the code is now broken down under HAVE_SNPRINTF and HAVE_VSNPRINTF.
 *
 *  Andrew Tridgell <tridge@samba.org> Oct 1998
 *    fixed handling of %.0f
 *    added test for HAVE_LONG_DOUBLE
 *
 **************************************************************/

#include <kern/system.h>
#include <kern/symbol.h>
#include <kern/log.h>
#include <lib/string.h>
#include <vm/malloc.h>
#include <vm/layout.h> /* VM_IS_KERN */

/*
 * dopr(): poor man's version of doprintf
 */

/* format read states */
#define DP_S_DEFAULT 0
#define DP_S_FLAGS   1
#define DP_S_MIN	 2
#define DP_S_DOT	 3
#define DP_S_MAX	 4
#define DP_S_MOD	 5
#define DP_S_CONV	6
#define DP_S_DONE	7

/* format flags - Bits */
#define DP_F_MINUS 	(1 << 0)
#define DP_F_PLUS  	(1 << 1)
#define DP_F_SPACE 	(1 << 2)
#define DP_F_NUM   	(1 << 3)
#define DP_F_ZERO  	(1 << 4)
#define DP_F_UP		(1 << 5)
#define DP_F_UNSIGNED 	(1 << 6)

/* Conversion Flags */
#define DP_C_SHORT   1
#define DP_C_LONG	2
#define DP_C_LDOUBLE 3
#define DP_C_LLONG   4

#define char_to_int(p) ((p) - '0')

static void dopr_outch(char *buffer, size_t *currlen, size_t maxlen, char c) {
	if(*currlen < maxlen) {
		buffer[(*currlen)] = c;
	}

	(*currlen)++;
}

static void fmtstr(char *buffer, size_t *currlen, size_t maxlen,
	char *value, int flags, int min, int max)
{
	int padlen, strln;	 /* amount to pad */
	int cnt = 0;

	if(value == 0) {
		value = "<NULL>";
	}

	/* strlen */
	for(strln = 0; value[strln]; ++strln) {
		continue;
	}

	padlen = min - strln;
	if(padlen < 0) {
		padlen = 0;
	}

	if(flags & DP_F_MINUS) {
		padlen = -padlen; /* Left Justify */
	}
	
	while((padlen > 0) && (cnt < max)) {
		dopr_outch(buffer, currlen, maxlen, ' ');
		--padlen;
		++cnt;
	}

	while(*value && (cnt < max)) {
		dopr_outch(buffer, currlen, maxlen, *value++);
		++cnt;
	}

	while((padlen < 0) && (cnt < max)) {
		dopr_outch(buffer, currlen, maxlen, ' ');
		++padlen;
		++cnt;
	}
}

/* Have to handle DP_F_NUM (ie 0x and 0 alternates) */
static void fmtint(char *buffer, size_t *currlen, size_t maxlen,
	long long value, int base, int min, int max, int flags)
{
	int signvalue = 0;
	unsigned long long uvalue;
	char convert[20];
	int place = 0;
	int spadlen = 0; /* amount to space pad */
	int zpadlen = 0; /* amount to zero pad */
	int caps = 0;
	
	if(max < 0) {
		max = 0;
	}
	
	uvalue = value;

	if(!(flags & DP_F_UNSIGNED)) {
		if(value < 0 ) {
			signvalue = '-';
			uvalue = -value;
		} else {
			if(flags & DP_F_PLUS) {  /* Do a sign (+/i) */
				signvalue = '+';
			} else if(flags & DP_F_SPACE) {
				signvalue = ' ';
			}
		}
	}
  
	if(flags & DP_F_UP) {
		caps = 1; /* Should characters be upper case? */
	}

	do {
		convert[place++] =
			(caps? "0123456789ABCDEF":"0123456789abcdef")
			[uvalue % (unsigned)base];
		uvalue = (uvalue / (unsigned)base);
	} while(uvalue && (place < 20));

	if(place == 20) {
		place--;
	}

	convert[place] = 0;

	zpadlen = max - place;
	spadlen = min - MAX (max, place) - (signvalue ? 1 : 0);
	if(zpadlen < 0) {
		zpadlen = 0;
	}
	
	if(spadlen < 0) {
		spadlen = 0;
	}

	if(flags & DP_F_ZERO) {
		zpadlen = MAX(zpadlen, spadlen);
		spadlen = 0;
	}

	if(flags & DP_F_MINUS) {
		spadlen = -spadlen; /* Left Justifty */
	}

	/* Spaces */
	while(spadlen > 0) {
		dopr_outch(buffer, currlen, maxlen, ' ');
		--spadlen;
	}

	/* Sign */
	if(signvalue) 
		dopr_outch(buffer, currlen, maxlen, signvalue);

	/* Zeros */
	if(zpadlen > 0) {
		while(zpadlen > 0) {
			dopr_outch(buffer, currlen, maxlen, '0');
			--zpadlen;
		}
	}

	/* Digits */
	while(place > 0) {
		dopr_outch(buffer, currlen, maxlen, convert[--place]);
	}
  
	/* Left Justified spaces */
	while(spadlen < 0) {
		dopr_outch(buffer, currlen, maxlen, ' ');
		++spadlen;
	}
}

int vsnprintf(char *buffer, size_t maxlen, const char *format,
	va_list args)
{
	long long value;
	char ch;
	char *strvalue;
	int min;
	int max;
	int state;
	int flags;
	int cflags;
	size_t currlen;

	if(!VM_IS_KERN(buffer)) {
		log_panic("[vsnprintf] buffer pointer invalid\n");
	} else if(!VM_IS_KERN(format)) {
		log_panic("[vsnprintf] format pointer invalid\n");
	}

	state = DP_S_DEFAULT;
	currlen = flags = cflags = min = 0;
	max = -1;
	ch = *format++;
	
	while(state != DP_S_DONE) {
		if(ch == '\0') {
			state = DP_S_DONE;
		}

		switch(state) {
		case DP_S_DEFAULT:
			if(ch == '%') {
				state = DP_S_FLAGS;
			} else {
				dopr_outch(buffer, &currlen, maxlen, ch);
			}

			ch = *format++;
			break;
		case DP_S_FLAGS:
			switch(ch) {
			case '-':
				flags |= DP_F_MINUS;
				ch = *format++;
				break;
			case '+':
				flags |= DP_F_PLUS;
				ch = *format++;
				break;
			case ' ':
				flags |= DP_F_SPACE;
				ch = *format++;
				break;
			case '#':
				flags |= DP_F_NUM;
				ch = *format++;
				break;
			case '0':
				flags |= DP_F_ZERO;
				ch = *format++;
				break;
			default:
				state = DP_S_MIN;
				break;
			}
			break;
		case DP_S_MIN:
			if(isdigit((unsigned char)ch)) {
				min = 10*min + char_to_int(ch);
				ch = *format++;
			} else if(ch == '*') {
				min = va_arg(args, int);
				ch = *format++;
				state = DP_S_DOT;
			} else {
				state = DP_S_DOT;
			}
			break;
		case DP_S_DOT:
			if(ch == '.') {
				state = DP_S_MAX;
				ch = *format++;
			} else { 
				state = DP_S_MOD;
			}
			break;
		case DP_S_MAX:
			if(isdigit((unsigned char)ch)) {
				if(max < 0) {
					max = 0;
				}

				max = 10 * max + char_to_int(ch);
				ch = *format++;
			} else if(ch == '*') {
				max = va_arg(args, int);
				ch = *format++;
				state = DP_S_MOD;
			} else {
				state = DP_S_MOD;
			}
			break;
		case DP_S_MOD:
			switch(ch) {
			case 'h':
				cflags = DP_C_SHORT;
				ch = *format++;
				break;
			case 'l':
				cflags = DP_C_LONG;
				ch = *format++;
				if(ch == 'l') {	/* It's a long long */
					cflags = DP_C_LLONG;
					ch = *format++;
				}
				break;
			case 'L':
				cflags = DP_C_LDOUBLE;
				ch = *format++;
				break;
			default:
				break;
			}
			state = DP_S_CONV;
			break;
		case DP_S_CONV:
			switch(ch) {
			case 'd':
			case 'i':
				if(cflags == DP_C_SHORT) {
					value = va_arg(args, int);
				} else if(cflags == DP_C_LONG) {
					value = va_arg(args, long int);
				} else if(cflags == DP_C_LLONG) {
					value = va_arg(args, long long);
				} else {
					value = va_arg(args, int);
				}

				fmtint(buffer, &currlen, maxlen, value, 10, min, max, flags);
				break;
			case 'o':
				flags |= DP_F_UNSIGNED;
				if(cflags == DP_C_SHORT) {
					value = (long long)va_arg(args, unsigned int);
				} else if(cflags == DP_C_LONG) {
					value = (long long)va_arg(args, unsigned long int);
				} else if(cflags == DP_C_LLONG) {
					value = (long long)va_arg(args, unsigned long long);
				} else {
					value = (long long)va_arg(args, unsigned int);
				}

				fmtint(buffer, &currlen, maxlen, value, 8, min, max, flags);
				break;
			case 'u':
				flags |= DP_F_UNSIGNED;
				if(cflags == DP_C_SHORT) {
					value = (long long)va_arg(args, unsigned int);
				} else if(cflags == DP_C_LONG) {
					value = (long long)va_arg(args, unsigned long int);
				} else if(cflags == DP_C_LLONG) {
					value = (long long)va_arg(args, unsigned long long);
				} else {
					value = (long long)va_arg(args, unsigned int);
				}

				fmtint(buffer, &currlen, maxlen, value, 10, min, max, flags);
				break;
			case 'X':
				flags |= DP_F_UP;
				/* FALLTHROUGH */
			case 'x':
				flags |= DP_F_UNSIGNED;
				
				if(cflags == DP_C_SHORT) {
					value = (long long)va_arg(args, unsigned int);
				} else if(cflags == DP_C_LONG) {
					value = (long long)va_arg(args, unsigned long int);
				} else if(cflags == DP_C_LLONG) {
					value = (long long)va_arg(args, unsigned long long);
				} else {
					value = (long long)va_arg(args, unsigned int);
				}

				fmtint(buffer, &currlen, maxlen, value, 16, min, max, flags);
				break;
			case 'c':
				dopr_outch(buffer, &currlen, maxlen, va_arg (args, int));
				break;
			case 's':
				strvalue = va_arg(args, char *);
				if(!strvalue) {
					strvalue = "(NULL)";
				} else if(!VM_IS_KERN(strvalue)) {
					strvalue = "(INVALID)";
				}

				if(max == -1) {
					max = strlen(strvalue);
				}
				if(min > 0 && max >= 0 && min > max) {
					max = min;
				}
				fmtstr(buffer, &currlen, maxlen, strvalue, flags, min, max);
				break;
			case 'p':
				flags |= DP_F_UNSIGNED;
				strvalue = va_arg(args, void *);
				fmtint(buffer, &currlen, maxlen,
					(long long)(uintptr_t)strvalue, 16, min, max, flags);
				break;
			case 'n':
				if(cflags == DP_C_SHORT) {
					short int *num;
					num = va_arg(args, short int *);
					*num = currlen;
				} else if(cflags == DP_C_LONG) {
					long int *num;
					num = va_arg(args, long int *);
					*num = (long int)currlen;
				} else if(cflags == DP_C_LLONG) {
					long long *num;
					num = va_arg(args, long long *);
					*num = (long long)currlen;
				} else {
					int *num;
					num = va_arg(args, int *);
					*num = currlen;
				}
				break;
			case '%':
				dopr_outch(buffer, &currlen, maxlen, ch);
				break;
			case 'w':
				/* not supported yet, treat as next char */
				ch = *format++;
				break;
			default:
				/* Unknown, skip */
				break;
			}

			ch = *format++;
			state = DP_S_DEFAULT;
			flags = cflags = min = 0;
			max = -1;
			break;
		case DP_S_DONE:
			break;
		default:
			/* hmm? */
			break; /* some picky compilers need this */
		}
	}

	if(maxlen != 0) {
		if(currlen < maxlen - 1) {
			buffer[currlen] = '\0';
		} else if(maxlen > 0) {
			buffer[maxlen - 1] = '\0';
		}
	}
	
	return currlen;
}
export(vsnprintf);

int snprintf(char *str, size_t count, const char *fmt, ...) {
	size_t ret;
	va_list ap;
	
	va_start(ap, fmt);
	ret = vsnprintf(str, count, fmt, ap);
	va_end(ap);
	return ret;
}
export(snprintf);

int vasprintf(char **ptr, const char *format, va_list ap) {
	int ret;
	va_list ap2;

	va_copy(ap2, ap);
	ret = vsnprintf(NULL, 0, format, ap2);
	if(ret <= 0) {
		return ret;
	}

	*ptr = kmalloc(ret + 1, VM_NOFLAG);
	if(!*ptr) {
		return -1;
	}

	va_copy(ap2, ap);
	ret = vsnprintf(*ptr, ret + 1, format, ap2);
	return ret;
}
export(vasprintf);

int asprintf(char **ptr, const char *format, ...) {
	va_list ap;
	int ret;
	
	*ptr = NULL;
	va_start(ap, format);
	ret = vasprintf(ptr, format, ap);
	va_end(ap);

	return ret;
}
export(asprintf);

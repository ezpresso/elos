#ifndef LIB_ASCII_H
#define LIB_ASCII_H

#define ASCII_CCLASSMASK	0x3f
#define	ASCII_ORDINARY		0
#define	ASCII_CONTROL		1
#define	ASCII_BACKSPACE		2
#define	ASCII_NEWLINE		3
#define	ASCII_TAB		4
#define	ASCII_VTAB		5
#define	ASCII_RETURN		6

#define	ASCII_ALPHA		0x40

#define	ASCII_E			0x00	/* Even parity. */
#define	ASCII_O			0x80	/* Odd parity. */

#define ASCII_CINFO(c)		assci_cinfo[(uint8_t)(c)]
#define ASCII_CLASS(c)		(ASCII_CINFO(c) & ASCII_CCLASSMASK)
#define ASCII_ISALPHA(c)	(ASCII_CINFO(c) & ASCII_ALPHA)
#define	ASCII_PARITY(c)		(ASCII_CINFO(c) & ASCII_O)

/**
 * @brief A table with information about the individual ASCII characters.
 *
 * Copied from NetBSD.
 */
extern const uint8_t assci_cinfo[256];

#endif
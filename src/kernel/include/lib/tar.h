#ifndef LIB_TAR_H
#define LIB_TAR_H

#define TMAGIC		"ustar"
#define TMAGLEN		6

#define TVERSION	"00"
#define TVERSLEN	2

#define REGTYPE		'0'
#define AREGTYPE	'\0'
#define LNKTYPE		'1'
#define SYMTYPE		'2'
#define CHRTYPE		'3'
#define BLKTYPE		'4'
#define DIRTYPE		'5'
#define FIFOTYPE	'6'
#define CONTTYPE	'7'

#define TSUID	04000
#define TSGID	02000
#define TSVTX	01000
#define TUREAD	00400
#define TUWRITE	00200
#define TUEXEC	00100
#define TGREAD	00040
#define TGWRITE	00020
#define TGEXEC	00010
#define TOREAD	00004
#define TOWRITE	00002
#define TOEXEC	00001

typedef struct tar_header {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char chksum[8];
	char typeflag;
	char linkname[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];
} tar_header_t;

static inline size_t tar_number(char *ptr, size_t size) {
	size_t num = 0, shift = 0;
	int j;

	for(j = (size - 2); j >= 0; j--, shift += 3) {
		num += (ptr[j] - '0') << shift;
	}
	
	return num;
}

static inline size_t tar_get_size(tar_header_t *hdr) {
	return tar_number(hdr->size, sizeof(hdr->size));
}

static inline void *tar_get_data(tar_header_t *hdr) {
	return (void *)hdr + 512;
}

static inline tar_header_t *tar_next_header(tar_header_t *hdr) {
	size_t size;

	if(hdr->name[0] == '\0') {
		return NULL;
	}

	size = ALIGN(tar_get_size(hdr), 512);
	return tar_get_data(hdr) + size;
}

#endif
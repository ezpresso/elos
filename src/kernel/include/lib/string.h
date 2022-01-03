#ifndef KERN_STRING_H
#define KERN_STRING_H

#include <sys/stdarg.h>

#define	isalnum(c) (isalpha(c) || isdigit(c))
#define	isalpha(c) (islower(c) || isupper(c))
#define	isblank(c) ((c) == ' ' || (c) == '\t')
#define	iscntrl(c) ((c) >= 0x0 && (c) <= 0x8)
#define	isdigit(c) ((c) >= '0' && (c) <= '9')
#define	isgraph(c) (ispunct(c) || isalnum(c))
#define	islower(c) ((c) >= 'a' && (c) <= 'z')
#define	isprint(c) (isgraph(c) || isspace(c))
#define	tolower(c) (isupper(c) ? ((c) + 'a' - 'A') : (c))
#define	toupper(c) (islower(c) ? ((c) + 'A' - 'a') : (c))
#define	isupper(c) ((c) >= 'A' && (c) <= 'Z')

#define	ispunct(c) (((c) >= 0x21 && (c) <= 0x2F) || 			\
	((c) >= 0x3A && (c) <= 0x40) || ((c) >= 0x5B && (c) <= 0x60) || \
	((c) >= 0x7B && (c) <= 0x7E))

#define	isspace(c) ((c) == ' ' || (c) == '\t' || (c) == '\r' || \
	(c) == '\n' || (c) == '\f' || (c) == '\v')

#define	isxdigit(c) (isdigit(c) || ((c) >= 'a' && (c) <= 'f') || \
		((c) >= 'A' && (c) <= 'F'))

#define	memcpy(d,s,n) 	__builtin_memcpy(d,s,n)
#define memmove(d,s,n)	__builtin_memmove(d,s,n)
#define	strcpy(d,s) 	__builtin_strcpy(d,s)
#define	strncpy(d,s,n)	Consider using strlcpy instead
#define	strcat(d,s)	__builtin_strcat(d,s)
#define	strncat(d,s,n)	__builtin_strncat(d,s,n)
#define	memcmp(a,b,n)	__builtin_memcmp(a,b,n)
#define	strcmp(a,b)	__builtin_strcmp(a,b)
#define	strncmp(a,b,n)	__builtin_strncmp(a,b,n)
#define	memchr(s,c,n)	__builtin_memchr(s,c,n)
#define	strchr(s,c)	__builtin_strchr(s,c)
#define	strcspn(a,b)	__builtin_strcspn(a,b)
#define	strpbrk(a,b)	__builtin_strpbrk(a,b)
#define	strrchr(s,c)	__builtin_strrchr(s,c)
#define	strspn(a,b)	__builtin_strspn(a,b)
#define	strstr(a,b)	__builtin_strstr(a,b)
#define	memset(s,c,n)	__builtin_memset(s,c,n)
#define	strlen(s)	__builtin_strlen(s)

int snprintf(char *str,size_t count,const char *fmt, ...);
int vsnprintf(char *str, size_t count, const char *fmt, va_list args);
int vasprintf(char **ptr, const char *format, va_list ap);
int asprintf(char **ptr, const char *format, ...);

void *memrchr(const void *s, int c, size_t n);
int strncasecmp(const char *s1, const char *s2, size_t len);
int strcasecmp(const char *s1, const char *s2);
size_t strnlen(const char *s, size_t maxlen);
char *concat(char *str1, char *str2);
size_t strlcpy(char *dst, const char *src, size_t max);
void strreverse(char *str);

#endif
/* AmiRocket - small string helpers that libnix and POSIX disagree on. */
#ifndef AR_UTIL_H
#define AR_UTIL_H

#include <stddef.h>

int    ar_strcasecmp(const char *a, const char *b);
int    ar_strncasecmp(const char *a, const char *b, size_t n);
size_t ar_strlcpy(char *dst, const char *src, size_t size);
char  *ar_trim(char *s);   /* in place, returns s */

#endif

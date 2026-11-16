#include "util.h"

#include <string.h>

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int ar_strcasecmp(const char *a, const char *b)
{
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) {
        a++;
        b++;
    }
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}

int ar_strncasecmp(const char *a, const char *b, size_t n)
{
    while (n && *a && lower((unsigned char)*a) == lower((unsigned char)*b)) {
        a++;
        b++;
        n--;
    }
    if (!n)
        return 0;
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}

size_t ar_strlcpy(char *dst, const char *src, size_t size)
{
    size_t n = strlen(src);

    if (size) {
        size_t c = n < size - 1 ? n : size - 1;
        memcpy(dst, src, c);
        dst[c] = 0;
    }
    return n;
}

char *ar_trim(char *s)
{
    char *b = s, *e;

    while (*b == ' ' || *b == '\t' || *b == '\r' || *b == '\n')
        b++;
    if (b != s)
        memmove(s, b, strlen(b) + 1);
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = 0;
    return s;
}

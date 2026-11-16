#include "buf.h"
#include "mem.h"

#include <string.h>
#include <stdio.h>

void buf_init(Buf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->oom = 0;
}

void buf_free(Buf *b)
{
    ar_free(b->data);
    buf_init(b);
}

void buf_clear(Buf *b)
{
    b->len = 0;
    if (b->data)
        b->data[0] = 0;
}

int buf_reserve(Buf *b, size_t extra)
{
    size_t need, ncap;
    char *n;

    if (b->oom)
        return -1;
    need = b->len + extra + 1;
    if (need <= b->cap)
        return 0;
    ncap = b->cap ? b->cap : 64;
    while (ncap < need)
        ncap *= 2;
    n = (char *)ar_realloc(b->data, ncap);
    if (!n) {
        b->oom = 1;
        return -1;
    }
    b->data = n;
    b->cap = ncap;
    return 0;
}

int buf_append(Buf *b, const void *p, size_t n)
{
    if (buf_reserve(b, n) < 0)
        return -1;
    if (n)
        memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = 0;
    return 0;
}

int buf_putc(Buf *b, char c)
{
    return buf_append(b, &c, 1);
}

int buf_puts(Buf *b, const char *s)
{
    return buf_append(b, s, strlen(s));
}

int buf_vprintf(Buf *b, const char *fmt, va_list ap)
{
    char tmp[256];
    va_list ap2;
    int n;

    va_copy(ap2, ap);
    n = vsnprintf(tmp, sizeof(tmp), fmt, ap2);
    va_end(ap2);
    if (n < 0)
        return -1;
    if ((size_t)n < sizeof(tmp))
        return buf_append(b, tmp, (size_t)n);
    if (buf_reserve(b, (size_t)n) < 0)
        return -1;
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    b->len += (size_t)n;
    return 0;
}

int buf_printf(Buf *b, const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = buf_vprintf(b, fmt, ap);
    va_end(ap);
    return r;
}

const char *buf_str(const Buf *b)
{
    return b->data ? b->data : "";
}

char *buf_detach(Buf *b)
{
    char *p = b->data;

    if (!p)
        p = ar_strdup("");
    buf_init(b);
    return p;
}

void buf_consume(Buf *b, size_t n)
{
    if (n >= b->len) {
        buf_clear(b);
        return;
    }
    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
    b->data[b->len] = 0;
}

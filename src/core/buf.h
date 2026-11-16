/* AmiRocket - growable byte buffer, always NUL terminated. */
#ifndef AR_BUF_H
#define AR_BUF_H

#include <stddef.h>
#include <stdarg.h>

typedef struct {
    char  *data;   /* NUL terminated when non-NULL */
    size_t len;
    size_t cap;
    int    oom;    /* set once an allocation failed; further appends are ignored */
} Buf;

void buf_init(Buf *b);
void buf_free(Buf *b);
void buf_clear(Buf *b);
int  buf_reserve(Buf *b, size_t extra);
int  buf_append(Buf *b, const void *p, size_t n);
int  buf_putc(Buf *b, char c);
int  buf_puts(Buf *b, const char *s);
int  buf_printf(Buf *b, const char *fmt, ...);
int  buf_vprintf(Buf *b, const char *fmt, va_list ap);
const char *buf_str(const Buf *b);   /* never NULL */
char *buf_detach(Buf *b);            /* caller owns result (ar_free) */
void buf_consume(Buf *b, size_t n);  /* drop n bytes from the front */

#endif

#include "../core/mem.h"

#include <stdlib.h>
#include <string.h>

int ar_mem_init(void) { return 0; }
void ar_mem_cleanup(void) { }

void *ar_malloc(size_t n) { return malloc(n ? n : 1); }
void *ar_calloc(size_t n, size_t sz) { return calloc(n ? n : 1, sz ? sz : 1); }
void *ar_realloc(void *p, size_t n) { return realloc(p, n ? n : 1); }
void ar_free(void *p) { free(p); }

char *ar_strdup(const char *s)
{
    return s ? ar_strndup(s, strlen(s)) : NULL;
}

char *ar_strndup(const char *s, size_t n)
{
    char *p;
    if (!s)
        return NULL;
    p = (char *)malloc(n + 1);
    if (!p)
        return NULL;
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

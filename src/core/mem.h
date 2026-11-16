/*
 * AmiRocket - memory allocation shim.
 *
 * All core code allocates through these functions instead of malloc().
 * On AmigaOS the C runtime allocator (libnix) is not safe to call from
 * two tasks at once, and AmiRocket runs its network code in a separate
 * process, so the Amiga implementation uses a semaphore-protected exec
 * memory pool. The POSIX implementation simply wraps malloc().
 */
#ifndef AR_MEM_H
#define AR_MEM_H

#include <stddef.h>

int   ar_mem_init(void);      /* returns 0 on success */
void  ar_mem_cleanup(void);   /* frees everything still allocated (Amiga) */

void *ar_malloc(size_t n);
void *ar_calloc(size_t n, size_t sz);
void *ar_realloc(void *p, size_t n);
void  ar_free(void *p);
char *ar_strdup(const char *s);
char *ar_strndup(const char *s, size_t n);

#endif

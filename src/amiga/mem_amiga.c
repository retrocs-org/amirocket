/*
 * AmiRocket - task-safe allocator for AmigaOS.
 *
 * libnix malloc() keeps unprotected global state, and AmiRocket allocates
 * from both the GUI task and the network process. Everything goes through
 * one exec memory pool guarded by a SignalSemaphore instead. Each block
 * carries its size in front so ar_realloc() can copy. ar_mem_cleanup()
 * deletes the pool, which releases anything that leaked.
 */
#include "../core/mem.h"

#include <exec/memory.h>
#include <exec/semaphores.h>
#include <proto/exec.h>

#include <string.h>

static APTR g_pool;
static struct SignalSemaphore g_sem;

#define HDR 8   /* keeps 8-byte payload alignment */

int ar_mem_init(void)
{
    if (g_pool)
        return 0;
    InitSemaphore(&g_sem);
    g_pool = CreatePool(MEMF_ANY, 32768, 8192);
    return g_pool ? 0 : -1;
}

void ar_mem_cleanup(void)
{
    if (g_pool) {
        DeletePool(g_pool);
        g_pool = NULL;
    }
}

void *ar_malloc(size_t n)
{
    ULONG *p;

    if (!g_pool)
        return NULL;
    if (n == 0)
        n = 1;
    ObtainSemaphore(&g_sem);
    p = (ULONG *)AllocPooled(g_pool, (ULONG)(n + HDR));
    ReleaseSemaphore(&g_sem);
    if (!p)
        return NULL;
    p[0] = (ULONG)(n + HDR);
    return (char *)p + HDR;
}

void ar_free(void *ptr)
{
    ULONG *p;

    if (!ptr || !g_pool)
        return;
    p = (ULONG *)((char *)ptr - HDR);
    ObtainSemaphore(&g_sem);
    FreePooled(g_pool, p, p[0]);
    ReleaseSemaphore(&g_sem);
}

void *ar_calloc(size_t n, size_t sz)
{
    size_t total = n * sz;
    void *p;

    if (sz && total / sz != n)
        return NULL;
    p = ar_malloc(total);
    if (p)
        memset(p, 0, total ? total : 1);
    return p;
}

void *ar_realloc(void *ptr, size_t n)
{
    void *np;
    size_t old;

    if (!ptr)
        return ar_malloc(n);
    old = ((ULONG *)((char *)ptr - HDR))[0] - HDR;
    if (n <= old && n > old / 2)
        return ptr;   /* shrinking a little: keep the block */
    np = ar_malloc(n);
    if (!np)
        return NULL;
    memcpy(np, ptr, old < n ? old : n);
    ar_free(ptr);
    return np;
}

char *ar_strndup(const char *s, size_t n)
{
    char *p;

    if (!s)
        return NULL;
    p = (char *)ar_malloc(n + 1);
    if (!p)
        return NULL;
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

char *ar_strdup(const char *s)
{
    return s ? ar_strndup(s, strlen(s)) : NULL;
}

#include "test_core.h"
#include "../src/core/mem.h"

#include <stdio.h>

static void out(const char *s)
{
    fputs(s, stdout);
}

int main(void)
{
    int f;

    ar_mem_init();
    f = run_core_tests(out);
    ar_mem_cleanup();
    return f ? 1 : 0;
}

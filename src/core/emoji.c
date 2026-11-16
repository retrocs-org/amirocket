#include "emoji.h"
#include "util.h"

#include <string.h>

int emoji_by_cp(unsigned long cp)
{
    int i;
    for (i = 0; i < emoji_count; i++)
        if (emoji_table[i].cp == cp)
            return i;
    return -1;
}

int emoji_by_code(const char *code, size_t len)
{
    int i;
    for (i = 0; i < emoji_count; i++)
        if (strlen(emoji_table[i].code) == len && !memcmp(emoji_table[i].code, code, len))
            return i;
    for (i = 0; emoji_aliases[i].alias; i++)
        if (strlen(emoji_aliases[i].alias) == len && !memcmp(emoji_aliases[i].alias, code, len))
            return emoji_aliases[i].idx;
    return -1;
}

int emoji_complete(const char *prefix, int *out, int max)
{
    size_t pl = strlen(prefix);
    int i, j, n = 0;

    for (i = 0; i < emoji_count && n < max; i++)
        if (!ar_strncasecmp(emoji_table[i].code, prefix, pl))
            out[n++] = i;
    for (i = 0; emoji_aliases[i].alias && n < max; i++) {
        int dup = 0;
        if (ar_strncasecmp(emoji_aliases[i].alias, prefix, pl))
            continue;
        for (j = 0; j < n; j++)
            if (out[j] == emoji_aliases[i].idx)
                dup = 1;
        if (!dup)
            out[n++] = emoji_aliases[i].idx;
    }
    return n;
}

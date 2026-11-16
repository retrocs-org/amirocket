#include "mdtext.h"
#include "emoji.h"

#include <string.h>

/* Appends the UTF-8 encoding of a code point. */
static void put_cp(Buf *out, unsigned long cp)
{
    char t[4];
    size_t n;
    if (cp < 0x80) { t[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { t[0] = (char)(0xC0 | (cp >> 6)); t[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) { t[0] = (char)(0xE0 | (cp >> 12)); t[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                             t[2] = (char)(0x80 | (cp & 0x3F)); n = 3; }
    else { t[0] = (char)(0xF0 | (cp >> 18)); t[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
           t[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); t[3] = (char)(0x80 | (cp & 0x3F)); n = 4; }
    buf_append(out, t, n);
}

const char *md_emoticon(const char *code)
{
    int e = emoji_by_code(code, strlen(code));
    return e >= 0 ? emoji_table[e].fallback : NULL;
}

static int is_word(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c >= 0x80;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == 0;
}

static void inline_md(Buf *out, const char *s, size_t n, int depth);

/* Emphasis run starting at s[i]: returns run length if it opens a span that
 * closes later in [s, s+n), storing the inner range. */
static size_t emphasis(const char *s, size_t n, size_t i, size_t *in_start, size_t *in_end)
{
    char m = s[i];
    size_t run = 1, j;

    if (i + 1 < n && s[i + 1] == m)
        run = 2;
    if (i > 0 && is_word((unsigned char)s[i - 1]))
        return 0;                       /* must not start inside a word */
    if (i + run >= n || is_space(s[i + run]) || s[i + run] == m)
        return 0;                       /* must be followed by text */
    for (j = i + run + 1; j + run <= n; j++) {
        if (s[j] != m)
            continue;
        if (run == 2 && (j + 1 >= n || s[j + 1] != m))
            continue;
        if (run == 1 && j + 1 < n && s[j + 1] == m)
            { j++; continue; }
        if (is_space(s[j - 1]))
            continue;                   /* closer must follow text */
        if (j + run < n && is_word((unsigned char)s[j + run]))
            continue;                   /* and must not run into a word */
        *in_start = i + run;
        *in_end = j;
        return run;
    }
    return 0;
}

static int shortcode(Buf *out, const char *s, size_t n, size_t i, size_t *used)
{
    size_t j = i + 1, k;

    if (i > 0 && !is_space(s[i - 1]) && s[i - 1] != '(')
        return 0;
    while (j < n && j - i <= 40 &&
           ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= '0' && s[j] <= '9') || s[j] == '_' || s[j] == '+' || s[j] == '-'))
        j++;
    if (j >= n || s[j] != ':' || j == i + 1)
        return 0;
    k = (size_t)emoji_by_code(s + i + 1, j - i - 1);
    if ((int)k >= 0) {
        put_cp(out, emoji_table[k].cp);   /* becomes an image or emoticon later */
        *used = j - i + 1;
        return 1;
    }
    return 0;
}

static int link(Buf *out, const char *s, size_t n, size_t i, size_t *used, int depth)
{
    size_t tend, uend;

    for (tend = i + 1; tend < n && s[tend] != ']' && s[tend] != '\n'; tend++)
        ;
    if (tend + 1 >= n || s[tend] != ']' || s[tend + 1] != '(')
        return 0;
    for (uend = tend + 2; uend < n && s[uend] != ')' && !is_space(s[uend]); uend++)
        ;
    if (uend >= n || s[uend] != ')' || uend == tend + 2)
        return 0;
    if (tend - i - 1 == uend - tend - 2 && !memcmp(s + i + 1, s + tend + 2, tend - i - 1)) {
        buf_append(out, s + tend + 2, uend - tend - 2);
    } else {
        inline_md(out, s + i + 1, tend - i - 1, depth + 1);
        buf_puts(out, " (");
        buf_append(out, s + tend + 2, uend - tend - 2);
        buf_putc(out, ')');
    }
    *used = uend - i + 1;
    return 1;
}

static void inline_md(Buf *out, const char *s, size_t n, int depth)
{
    size_t i = 0;

    while (i < n) {
        char c = s[i];
        size_t used, a, b, run;

        if (depth < 4) {
            if (c == '`') {
                const char *e = (const char *)memchr(s + i + 1, '`', n - i - 1);
                if (e) {
                    buf_append(out, s + i + 1, (size_t)(e - (s + i + 1)));
                    i = (size_t)(e - s) + 1;
                    continue;
                }
            } else if (c == '*' || c == '_' || c == '~') {
                run = emphasis(s, n, i, &a, &b);
                if (run) {
                    inline_md(out, s + a, b - a, depth + 1);
                    i = b + run;
                    continue;
                }
            } else if (c == '[' && link(out, s, n, i, &used, depth)) {
                i += used;
                continue;
            } else if (c == ':' && shortcode(out, s, n, i, &used)) {
                i += used;
                continue;
            } else if ((c == 'h' || c == 'w') && (i == 0 || !is_word((unsigned char)s[i - 1])) &&
                       (!strncmp(s + i, "http://", 7) || !strncmp(s + i, "https://", 8) || !strncmp(s + i, "www.", 4))) {
                /* copy URLs untouched so _ and * inside them survive */
                size_t j = i;
                while (j < n && !is_space(s[j]) && s[j] != '\n')
                    j++;
                buf_append(out, s + i, j - i);
                i = j;
                continue;
            }
        }
        buf_putc(out, c);
        i++;
    }
}

void md_to_plain(Buf *out, const char *s)
{
    int in_fence = 0, first = 1;

    while (s && *s) {
        const char *nl = strchr(s, '\n');
        size_t len = nl ? (size_t)(nl - s) : strlen(s);
        const char *t = s;
        size_t tl = len;

        while (tl && (*t == ' ' || *t == '\t')) {
            t++;
            tl--;
        }
        if (tl >= 3 && !strncmp(t, "```", 3)) {
            const char *close = tl > 6 ? strstr(t + 3, "```") : NULL;
            if (close && close < t + tl && !in_fence) {
                /* ```one line of code``` */
                if (!first)
                    buf_putc(out, '\n');
                buf_append(out, t + 3, (size_t)(close - t - 3));
                first = 0;
            } else {
                in_fence = !in_fence;
                if (!in_fence && tl > 3) {   /* text after a closing fence */
                    if (!first)
                        buf_putc(out, '\n');
                    inline_md(out, t + 3, tl - 3, 0);
                    first = 0;
                }
            }
        } else {
            if (!first)
                buf_putc(out, '\n');
            first = 0;
            if (in_fence) {
                buf_append(out, s, len);
            } else {
                size_t h = 0;
                while (h < tl && h < 6 && t[h] == '#')
                    h++;
                if (h && h < tl && t[h] == ' ')
                    inline_md(out, t + h + 1, tl - h - 1, 0);
                else
                    inline_md(out, s, len, 0);
            }
        }
        s = nl ? nl + 1 : s + len;
    }
}

#include "charset.h"

#include <string.h>

typedef struct {
    unsigned long cp;
    const char *s;
} CpMap;

/* Sorted by code point for binary search. */
static const CpMap map[] = {
    { 0x0152, "OE" }, { 0x0153, "oe" }, { 0x0160, "S" }, { 0x0161, "s" },
    { 0x0178, "Y" }, { 0x017D, "Z" }, { 0x017E, "z" }, { 0x0192, "f" },
    { 0x02C6, "^" }, { 0x02DC, "~" },
    { 0x2002, " " }, { 0x2003, " " }, { 0x2009, " " },
    { 0x200B, "" }, { 0x200C, "" }, { 0x200D, "" }, { 0x200E, "" }, { 0x200F, "" },
    { 0x2010, "-" }, { 0x2011, "-" }, { 0x2012, "-" }, { 0x2013, "-" }, { 0x2014, "-" }, { 0x2015, "-" },
    { 0x2018, "'" }, { 0x2019, "'" }, { 0x201A, "," }, { 0x201B, "'" },
    { 0x201C, "\"" }, { 0x201D, "\"" }, { 0x201E, "\"" }, { 0x201F, "\"" },
    { 0x2020, "+" }, { 0x2022, "\xB7" }, { 0x2026, "..." }, { 0x202F, " " },
    { 0x2030, "o/oo" }, { 0x2032, "'" }, { 0x2033, "\"" }, { 0x2039, "<" }, { 0x203A, ">" },
    { 0x2060, "" }, { 0x20AC, "EUR" }, { 0x2122, "(TM)" },
    { 0x2190, "<-" }, { 0x2192, "->" }, { 0x2194, "<->" }, { 0x21D2, "=>" },
    { 0x2212, "-" }, { 0x2248, "~" }, { 0x2260, "!=" }, { 0x2264, "<=" }, { 0x2265, ">=" },
    { 0x2600, "(sun)" }, { 0x2639, ":(" }, { 0x263A, ":)" },
    { 0x2705, "(v)" }, { 0x2713, "v" }, { 0x2714, "v" }, { 0x2716, "x" },
    { 0x274C, "x" }, { 0x2757, "!" }, { 0x2764, "<3" },
    { 0xFE0E, "" }, { 0xFE0F, "" }, { 0xFEFF, "" },
    { 0x1F389, "\\o/" }, { 0x1F440, "(eyes)" }, { 0x1F44B, "o/" },
    { 0x1F44C, "(ok)" }, { 0x1F44D, "(y)" }, { 0x1F44E, "(n)" }, { 0x1F44F, "(clap)" },
    { 0x1F4A9, "(poo)" }, { 0x1F4AA, "(strong)" }, { 0x1F525, "(fire)" },
    { 0x1F600, ":D" }, { 0x1F601, ":D" }, { 0x1F602, ":'D" }, { 0x1F603, ":D" },
    { 0x1F604, ":D" }, { 0x1F605, ":D" }, { 0x1F606, "XD" }, { 0x1F607, "O:)" },
    { 0x1F609, ";)" }, { 0x1F60A, ":)" }, { 0x1F60B, ":P" }, { 0x1F60D, "<3" },
    { 0x1F60E, "B)" }, { 0x1F610, ":|" }, { 0x1F612, ":/" }, { 0x1F615, ":/" },
    { 0x1F618, ":*" }, { 0x1F61B, ":P" }, { 0x1F61C, ";P" }, { 0x1F61E, ":(" },
    { 0x1F620, ">:(" }, { 0x1F621, ">:(" }, { 0x1F622, ":'(" }, { 0x1F62D, ":'(" },
    { 0x1F62E, ":O" }, { 0x1F631, ":O" }, { 0x1F641, ":(" }, { 0x1F642, ":)" },
    { 0x1F643, "(:" }, { 0x1F644, "(rolleyes)" }, { 0x1F64F, "(pray)" },
    { 0x1F680, "(rocket)" }, { 0x1F914, ":-?" }, { 0x1F923, "XD" },
    { 0x1F926, "(facepalm)" }, { 0x1F937, "(shrug)" }
};

static const char *lookup(unsigned long cp)
{
    int lo = 0, hi = (int)(sizeof(map) / sizeof(map[0])) - 1;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (map[mid].cp == cp)
            return map[mid].s;
        if (map[mid].cp < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

int utf8_decode(const unsigned char *s, unsigned long *cp)
{
    unsigned char c = s[0];
    int n, i;
    unsigned long v, min;

    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        n = 2; v = c & 0x1F; min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        n = 3; v = c & 0x0F; min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        n = 4; v = c & 0x07; min = 0x10000;
    } else {
        *cp = 0xFFFD;
        return 1;
    }
    for (i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return i;   /* resync at the offending byte */
        }
        v = (v << 6) | (s[i] & 0x3F);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
        v = 0xFFFD;
    *cp = v;
    return n;
}

void utf8_to_latin1(Buf *out, const char *utf8, int multiline)
{
    const unsigned char *s = (const unsigned char *)utf8;

    while (*s) {
        unsigned long cp;
        const char *rep;

        s += utf8_decode(s, &cp);
        if (cp == '\n') {
            buf_putc(out, multiline ? '\n' : ' ');
        } else if (cp == '\t') {
            buf_putc(out, ' ');
        } else if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) {
            /* control characters are dropped */
        } else if (cp < 0x100) {
            buf_putc(out, (char)cp);
        } else if ((rep = lookup(cp)) != NULL) {
            buf_puts(out, rep);
        } else if (cp >= 0x1F3FB && cp <= 0x1F3FF) {
            /* skin tone modifiers */
        } else if (cp >= 0xE0000 && cp <= 0xE007F) {
            /* tag characters (flag sequences) */
        } else {
            buf_putc(out, '?');
        }
    }
}

void latin1_to_utf8(Buf *out, const char *latin1)
{
    const unsigned char *s = (const unsigned char *)latin1;

    for (; *s; s++) {
        if (*s < 0x80) {
            buf_putc(out, (char)*s);
        } else {
            char t[2];
            t[0] = (char)(0xC0 | (*s >> 6));
            t[1] = (char)(0x80 | (*s & 0x3F));
            buf_append(out, t, 2);
        }
    }
}

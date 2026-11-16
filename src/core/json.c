#include "json.h"
#include "mem.h"

#include <string.h>

typedef struct {
    const char *s;
    int len;
    int pos;
    JTok *t;      /* NULL during the counting pass */
    int n;
    int depth;
    int err;
} JParser;

static void skip_ws(JParser *p)
{
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            p->pos++;
        else
            break;
    }
}

static int new_tok(JParser *p, int type, int start)
{
    int i = p->n++;
    if (p->t) {
        p->t[i].type = (unsigned char)type;
        p->t[i].escaped = 0;
        p->t[i].start = start;
        p->t[i].end = start;
        p->t[i].size = 0;
        p->t[i].next = i + 1;
    }
    return i;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_string(JParser *p)
{
    int start, i, esc = 0;

    p->pos++; /* opening quote */
    start = p->pos;
    while (p->pos < p->len) {
        unsigned char c = (unsigned char)p->s[p->pos];
        if (c == '"') {
            i = new_tok(p, JT_STRING, start);
            if (p->t) {
                p->t[i].end = p->pos;
                p->t[i].escaped = (unsigned char)esc;
            }
            p->pos++;
            return i;
        }
        if (c == '\\') {
            esc = 1;
            p->pos++;
            if (p->pos >= p->len)
                break;
            c = (unsigned char)p->s[p->pos];
            if (c == 'u') {
                int k;
                for (k = 1; k <= 4; k++)
                    if (p->pos + k >= p->len || hexval(p->s[p->pos + k]) < 0) {
                        p->err = 1;
                        return -1;
                    }
                p->pos += 4;
            } else if (!strchr("\"\\/bfnrt", c)) {
                p->err = 1;
                return -1;
            }
        } else if (c < 0x20) {
            p->err = 1;
            return -1;
        }
        p->pos++;
    }
    p->err = 1;
    return -1;
}

static int match_word(JParser *p, const char *w)
{
    int n = (int)strlen(w);
    if (p->pos + n > p->len || memcmp(p->s + p->pos, w, (size_t)n) != 0)
        return 0;
    p->pos += n;
    return 1;
}

static int parse_value(JParser *p);

static int parse_container(JParser *p, int is_obj)
{
    int i = new_tok(p, is_obj ? JT_OBJECT : JT_ARRAY, p->pos);
    int count = 0;
    char close = is_obj ? '}' : ']';

    if (++p->depth > JSON_MAX_DEPTH) {
        p->err = 1;
        return -1;
    }
    p->pos++;
    skip_ws(p);
    if (p->pos < p->len && p->s[p->pos] == close) {
        p->pos++;
        goto done;
    }
    for (;;) {
        skip_ws(p);
        if (is_obj) {
            if (p->pos >= p->len || p->s[p->pos] != '"') {
                p->err = 1;
                return -1;
            }
            if (parse_string(p) < 0)
                return -1;
            skip_ws(p);
            if (p->pos >= p->len || p->s[p->pos] != ':') {
                p->err = 1;
                return -1;
            }
            p->pos++;
        }
        if (parse_value(p) < 0)
            return -1;
        count++;
        skip_ws(p);
        if (p->pos >= p->len) {
            p->err = 1;
            return -1;
        }
        if (p->s[p->pos] == ',') {
            p->pos++;
            continue;
        }
        if (p->s[p->pos] == close) {
            p->pos++;
            break;
        }
        p->err = 1;
        return -1;
    }
done:
    p->depth--;
    if (p->t) {
        p->t[i].size = count;
        p->t[i].end = p->pos;
        p->t[i].next = p->n;
    }
    return i;
}

static int parse_number(JParser *p)
{
    int start = p->pos, i;

    if (p->pos < p->len && p->s[p->pos] == '-')
        p->pos++;
    if (p->pos >= p->len || p->s[p->pos] < '0' || p->s[p->pos] > '9') {
        p->err = 1;
        return -1;
    }
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
            p->pos++;
        else
            break;
    }
    i = new_tok(p, JT_NUMBER, start);
    if (p->t)
        p->t[i].end = p->pos;
    return i;
}

static int parse_value(JParser *p)
{
    int start, i;

    skip_ws(p);
    if (p->pos >= p->len) {
        p->err = 1;
        return -1;
    }
    start = p->pos;
    switch (p->s[p->pos]) {
    case '{': return parse_container(p, 1);
    case '[': return parse_container(p, 0);
    case '"': return parse_string(p);
    case 't':
        if (!match_word(p, "true")) break;
        i = new_tok(p, JT_TRUE, start);
        if (p->t) p->t[i].end = p->pos;
        return i;
    case 'f':
        if (!match_word(p, "false")) break;
        i = new_tok(p, JT_FALSE, start);
        if (p->t) p->t[i].end = p->pos;
        return i;
    case 'n':
        if (!match_word(p, "null")) break;
        i = new_tok(p, JT_NULL, start);
        if (p->t) p->t[i].end = p->pos;
        return i;
    default:
        return parse_number(p);
    }
    p->err = 1;
    return -1;
}

static int run_pass(JParser *p)
{
    p->pos = 0;
    p->n = 0;
    p->depth = 0;
    p->err = 0;
    if (parse_value(p) < 0 || p->err)
        return -1;
    skip_ws(p);
    if (p->pos != p->len)
        return -1;
    return 0;
}

int json_parse(JDoc *d, const char *js, size_t len)
{
    JParser p;

    memset(d, 0, sizeof(*d));
    d->js = js;
    d->len = len;
    d->err_pos = -1;
    if (len > 0x7fffffffUL)
        return -1;

    memset(&p, 0, sizeof(p));
    p.s = js;
    p.len = (int)len;
    if (run_pass(&p) < 0) {
        d->err_pos = p.pos;
        return -1;
    }
    d->t = (JTok *)ar_malloc(sizeof(JTok) * (size_t)p.n);
    if (!d->t)
        return -1;
    p.t = d->t;
    if (run_pass(&p) < 0) {
        ar_free(d->t);
        d->t = NULL;
        d->err_pos = p.pos;
        return -1;
    }
    d->n = p.n;
    return 0;
}

void json_free(JDoc *d)
{
    ar_free(d->t);
    d->t = NULL;
    d->n = 0;
}

static int valid(const JDoc *d, int i)
{
    return d && d->t && i >= 0 && i < d->n;
}

int json_type(const JDoc *d, int i)
{
    return valid(d, i) ? d->t[i].type : JT_INVALID;
}

int json_size(const JDoc *d, int i)
{
    return valid(d, i) ? d->t[i].size : 0;
}

int json_first(const JDoc *d, int c)
{
    if (!valid(d, c) || d->t[c].size == 0)
        return -1;
    if (d->t[c].type == JT_ARRAY)
        return c + 1;
    if (d->t[c].type == JT_OBJECT)
        return c + 2;   /* value of the first pair */
    return -1;
}

int json_next(const JDoc *d, int i)
{
    /* Sibling of an array item. (For object values this lands on the next key.) */
    int n;
    if (!valid(d, i))
        return -1;
    n = d->t[i].next;
    if (n >= d->n)
        return -1;
    return n;
}

/* Compare a string token with a C string, decoding escapes if needed. */
static int tok_eq(const JDoc *d, int i, const char *s)
{
    const JTok *t = &d->t[i];
    size_t sl = strlen(s);

    if (t->type != JT_STRING)
        return 0;
    if (!t->escaped)
        return (size_t)(t->end - t->start) == sl && memcmp(d->js + t->start, s, sl) == 0;
    {
        char tmp[128];
        int n = json_str(d, i, tmp, sizeof(tmp));
        return n >= 0 && (size_t)n == sl && memcmp(tmp, s, sl) == 0;
    }
}

int json_get(const JDoc *d, int obj, const char *key)
{
    int i, k;

    if (!valid(d, obj) || d->t[obj].type != JT_OBJECT)
        return -1;
    i = obj + 1;
    for (k = 0; k < d->t[obj].size; k++) {
        int val = i + 1;
        if (tok_eq(d, i, key))
            return val;
        i = d->t[val].next;
    }
    return -1;
}

int json_at(const JDoc *d, int arr, int idx)
{
    int i, k;

    if (!valid(d, arr) || d->t[arr].type != JT_ARRAY || idx < 0 || idx >= d->t[arr].size)
        return -1;
    i = arr + 1;
    for (k = 0; k < idx; k++)
        i = d->t[i].next;
    return i;
}

int json_path(const JDoc *d, int root, const char *dotted)
{
    char key[64];
    int cur = root;

    while (*dotted && cur >= 0) {
        size_t n = 0;
        while (dotted[n] && dotted[n] != '.')
            n++;
        if (n >= sizeof(key))
            return -1;
        memcpy(key, dotted, n);
        key[n] = 0;
        cur = json_get(d, cur, key);
        dotted += n;
        if (*dotted == '.')
            dotted++;
    }
    return cur;
}

int json_is_null(const JDoc *d, int i)
{
    return !valid(d, i) || d->t[i].type == JT_NULL;
}

int json_eq(const JDoc *d, int i, const char *s)
{
    return valid(d, i) && tok_eq(d, i, s);
}

static size_t put_utf8(char *o, unsigned long cp)
{
    if (cp < 0x80) {
        o[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        o[0] = (char)(0xC0 | (cp >> 6));
        o[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | (cp >> 12));
        o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        o[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (char)(0xF0 | (cp >> 18));
    o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    o[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static unsigned long hex4(const char *s)
{
    return (unsigned long)((hexval(s[0]) << 12) | (hexval(s[1]) << 8) | (hexval(s[2]) << 4) | hexval(s[3]));
}

/* Decode a string token. If out is NULL only the length is computed. */
static int decode(const JDoc *d, int i, char *out, size_t outsz)
{
    const JTok *t;
    const char *s, *e;
    size_t o = 0;
    char tmp[4];

    if (!valid(d, i) || d->t[i].type != JT_STRING)
        return -1;
    t = &d->t[i];
    s = d->js + t->start;
    e = d->js + t->end;
    while (s < e) {
        size_t n = 1;
        const char *src = s;
        if (*s == '\\') {
            s++;
            switch (*s) {
            case 'n': tmp[0] = '\n'; break;
            case 't': tmp[0] = '\t'; break;
            case 'r': tmp[0] = '\r'; break;
            case 'b': tmp[0] = '\b'; break;
            case 'f': tmp[0] = '\f'; break;
            case 'u': {
                unsigned long cp = hex4(s + 1);
                s += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && s + 6 < e && s[1] == '\\' && s[2] == 'u') {
                    unsigned long lo = hex4(s + 3);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        s += 6;
                    } else {
                        cp = 0xFFFD;
                    }
                } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                    cp = 0xFFFD;
                }
                n = put_utf8(tmp, cp);
                break;
            }
            default: tmp[0] = *s; break; /* " \ / */
            }
            src = tmp;
        }
        if (out) {
            if (o + n >= outsz)
                return -1;
            memcpy(out + o, src, n);
        }
        o += n;
        s++;
    }
    if (out)
        out[o] = 0;
    return (int)o;
}

int json_str(const JDoc *d, int i, char *out, size_t outsz)
{
    if (!out || outsz == 0)
        return -1;
    out[0] = 0;
    return decode(d, i, out, outsz);
}

char *json_strdup(const JDoc *d, int i)
{
    int n = decode(d, i, NULL, 0);
    char *p;

    if (n < 0)
        return NULL;
    p = (char *)ar_malloc((size_t)n + 1);
    if (!p)
        return NULL;
    decode(d, i, p, (size_t)n + 1);
    return p;
}

int json_bool(const JDoc *d, int i, int def)
{
    if (!valid(d, i))
        return def;
    if (d->t[i].type == JT_TRUE)
        return 1;
    if (d->t[i].type == JT_FALSE)
        return 0;
    return def;
}

long long json_ll(const JDoc *d, int i, long long def)
{
    const char *s, *e;
    long long v = 0;
    int neg = 0;

    if (!valid(d, i) || d->t[i].type != JT_NUMBER)
        return def;
    s = d->js + d->t[i].start;
    e = d->js + d->t[i].end;
    if (s < e && *s == '-') {
        neg = 1;
        s++;
    }
    while (s < e && *s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

void json_put_string(Buf *b, const char *s)
{
    static const char hex[] = "0123456789abcdef";

    buf_putc(b, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  buf_puts(b, "\\\""); break;
        case '\\': buf_puts(b, "\\\\"); break;
        case '\n': buf_puts(b, "\\n"); break;
        case '\r': buf_puts(b, "\\r"); break;
        case '\t': buf_puts(b, "\\t"); break;
        default:
            if (c < 0x20) {
                char u[7] = { '\\', 'u', '0', '0', 0, 0, 0 };
                u[4] = hex[c >> 4];
                u[5] = hex[c & 15];
                buf_append(b, u, 6);
            } else {
                buf_putc(b, (char)c);
            }
        }
    }
    buf_putc(b, '"');
}

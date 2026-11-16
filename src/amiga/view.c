#include "view.h"
#include "../core/charset.h"
#include "../core/mdtext.h"
#include "../core/mem.h"
#include "../core/timeutil.h"
#include "../core/util.h"

#include <graphics/text.h>
#include <proto/graphics.h>

#include <stdio.h>
#include <string.h>

enum { VL_GAP, VL_DAY, VL_HEADER, VL_TEXT, VL_SYS, VL_PILLS };

#define PAD          6
#define GROUP_GAP_MS (5LL * 60000LL)

/* ---------------------------------------------------------------- helpers */

static int line_height(struct RastPort *rp)
{
    return rp->TxHeight + 3;
}

static int avatar_size(int lh)
{
    return lh * 2 - 4;
}

static int text_indent(int lh)
{
    return PAD + avatar_size(lh) + 8;
}

static void text_at(struct RastPort *rp, int x, int y, const char *s, int len)
{
    if (len <= 0)
        return;
    Move(rp, x, y + rp->TxBaseline + 1);
    Text(rp, (CONST_STRPTR)s, len);
}

/* Characters of s that fit in maxw pixels. */
static int fit(struct RastPort *rp, const char *s, int len, int maxw)
{
    struct TextExtent te;
    if (maxw <= 0 || len <= 0)
        return 0;
    return (int)TextFit(rp, (CONST_STRPTR)s, (ULONG)len, &te, NULL, 1, (ULONG)maxw, 32767);
}

static void bold(struct RastPort *rp, int on)
{
    SetSoftStyle(rp, on ? FSF_BOLD : 0, AskSoftStyle(rp));
}

static void italic(struct RastPort *rp, int on)
{
    SetSoftStyle(rp, on ? FSF_ITALIC : 0, AskSoftStyle(rp));
}

/* ------------------------------------------------------------- message view */

void chat_init(ChatView *cv)
{
    memset(cv, 0, sizeof(*cv));
    buf_init(&cv->pool);
    cv->follow = 1;
}

void chat_free(ChatView *cv)
{
    buf_free(&cv->pool);
    ar_free(cv->v);
    memset(cv, 0, sizeof(*cv));
}

static VLine *add_line(ChatView *cv, int kind, const char *text, int len)
{
    VLine *l;

    if (cv->n == cv->cap) {
        int nc = cv->cap ? cv->cap * 2 : 256;
        VLine *nv = (VLine *)ar_realloc(cv->v, sizeof(VLine) * (size_t)nc);
        if (!nv)
            return NULL;
        cv->v = nv;
        cv->cap = nc;
    }
    l = &cv->v[cv->n++];
    memset(l, 0, sizeof(*l));
    l->kind = (UBYTE)kind;
    l->off = (LONG)cv->pool.len;
    if (len > 0x7fff)
        len = 0x7fff;
    l->len = (WORD)len;
    if (len > 0)
        buf_append(&cv->pool, text, (size_t)len);
    return l;
}

/* Word-wraps one paragraph into lines of at most maxw pixels. */
static void wrap(ChatView *cv, struct RastPort *rp, int kind, const char *s, int maxw, int hl)
{
    int len = (int)strlen(s);

    if (len == 0) {
        VLine *l = add_line(cv, kind, "", 0);
        if (l)
            l->hl = (UBYTE)hl;
        return;
    }
    while (len > 0) {
        int n = fit(rp, s, len, maxw), cut;
        VLine *l;
        if (n >= len) {
            cut = len;
        } else {
            cut = n;
            while (cut > 0 && s[cut] != ' ')
                cut--;
            if (cut == 0)
                cut = n > 0 ? n : 1;   /* one long word: break it */
        }
        l = add_line(cv, kind, s, cut);
        if (l)
            l->hl = (UBYTE)hl;
        s += cut;
        len -= cut;
        while (len > 0 && *s == ' ') {
            s++;
            len--;
        }
    }
}

static int mentions(const char *utf8, const char *me)
{
    char tag[100];
    const char *p;
    size_t n;

    if (!me || !*me || !utf8)
        return 0;
    snprintf(tag, sizeof(tag), "@%s", me);
    n = strlen(tag);
    for (p = utf8; (p = strchr(p, '@')) != NULL; p++) {
        size_t l = 0;
        if (!ar_strncasecmp(p, tag, n))
            l = n;
        else if (!strncmp(p, "@all", 4))
            l = 4;
        else if (!strncmp(p, "@here", 5))
            l = 5;
        if (l) {
            char c = p[l];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '.' || c == '_' || c == '-'))
                return 1;
        }
    }
    return 0;
}

static void day_label(char *out, size_t sz, long long local, long today)
{
    static const char *wd[] = { "Thursday", "Friday", "Saturday", "Sunday", "Monday", "Tuesday", "Wednesday" };
    static const char *mo[] = { "January", "February", "March", "April", "May", "June", "July",
                                "August", "September", "October", "November", "December" };
    long day = ms_day_number(local), y;
    unsigned m, d;

    if (day == today) {
        snprintf(out, sz, "Today");
        return;
    }
    if (day == today - 1) {
        snprintf(out, sz, "Yesterday");
        return;
    }
    civil_from_days(day, &y, &m, &d);
    snprintf(out, sz, "%s %u %s %ld", wd[((day % 7) + 7) % 7], d, mo[m - 1], y);
}

static int name_colour(const char *name)
{
    unsigned long h = 5381;
    while (*name)
        h = h * 33 + (unsigned char)*name++;
    return (int)(h % 6);
}

void chat_layout(ChatView *cv, struct RastPort *rp, int width, const RcMsgList *msgs,
                 long long tz_ms, long today, const char *me, const char *empty_text)
{
    int lh = line_height(rp), indent = text_indent(lh), maxw = width - indent - PAD, i;
    long lastday = -1;
    const char *prev_user = NULL;
    long long prev_ts = 0;
    Buf text, md, para;

    buf_clear(&cv->pool);
    cv->n = 0;
    cv->width = width;
    cv->line_h = lh;
    if (maxw < 40)
        maxw = 40;

    if (!msgs || msgs->count == 0) {
        if (empty_text)
            add_line(cv, VL_DAY, empty_text, (int)strlen(empty_text));
        return;
    }

    buf_init(&text);
    buf_init(&md);
    buf_init(&para);
    for (i = 0; i < msgs->count; i++) {
        const RcMsg *m = &msgs->items[i];
        long long local = m->ts_ms + tz_ms;
        long day = ms_day_number(local);
        const char *sys = rc_system_text(m->type);
        char hm[8];
        char *p, *nl;
        int hl;

        if (day != lastday) {
            char lab[48];
            if (cv->n)
                add_line(cv, VL_GAP, "", 0);
            day_label(lab, sizeof(lab), local, today);
            add_line(cv, VL_DAY, lab, (int)strlen(lab));
            lastday = day;
            prev_user = NULL;
        }
        ms_format_hm(local, hm);

        if (sys) {
            buf_clear(&text);
            utf8_to_latin1(&text, m->username, 0);
            buf_putc(&text, ' ');
            buf_puts(&text, sys);
            if (m->text && *m->text && strcmp(m->type, "rm") != 0) {
                buf_putc(&text, ' ');
                utf8_to_latin1(&text, m->text, 0);
            }
            buf_printf(&text, "  %s", hm);
            if (cv->n)
                add_line(cv, VL_GAP, "", 0);
            wrap(cv, rp, VL_SYS, buf_str(&text), maxw, 0);
            prev_user = NULL;
            continue;
        }

        if (!prev_user || strcmp(prev_user, m->username) != 0 || m->ts_ms - prev_ts > GROUP_GAP_MS || m->tmid) {
            VLine *h;
            int nl_len;
            if (cv->n)
                add_line(cv, VL_GAP, "", 0);
            buf_clear(&text);
            utf8_to_latin1(&text, m->username, 0);
            nl_len = (int)text.len;
            buf_puts(&text, hm);
            if (m->tmid)
                buf_puts(&text, "   reply in thread");
            h = add_line(cv, VL_HEADER, buf_str(&text), (int)text.len);
            if (h) {
                char c = m->username[0];
                h->name_len = (WORD)nl_len;
                h->avatar = (UBYTE)name_colour(m->username);
                h->initial = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
            }
        }
        prev_user = m->username;
        prev_ts = m->ts_ms;

        hl = mentions(m->text, me);
        buf_clear(&md);
        md_to_plain(&md, m->text);
        buf_clear(&text);
        utf8_to_latin1(&text, buf_str(&md), 1);
        if (m->n_attachments) {
            if (text.len)
                buf_putc(&text, '\n');
            buf_puts(&text, "[file: ");
            utf8_to_latin1(&text, m->attachment ? m->attachment : "attachment", 0);
            buf_putc(&text, ']');
        }
        if (m->edited)
            buf_puts(&text, "  (edited)");
        p = text.data ? text.data : (char *)"";
        for (;;) {
            nl = strchr(p, '\n');
            if (nl)
                *nl = 0;
            wrap(cv, rp, VL_TEXT, p, maxw, hl);
            if (!nl)
                break;
            p = nl + 1;
        }

        if (m->reactions) {   /* "code count" lines -> "<3 1\n(y) 2" pills */
            const char *r = m->reactions;
            buf_clear(&para);
            while (*r) {
                const char *e = strchr(r, '\n'), *sp;
                char code[64];
                size_t l = e ? (size_t)(e - r) : strlen(r);
                sp = (const char *)memchr(r, ' ', l);
                if (sp && (size_t)(sp - r) < sizeof(code)) {
                    const char *emo;
                    memcpy(code, r, (size_t)(sp - r));
                    code[sp - r] = 0;
                    emo = md_emoticon(code);
                    if (para.len)
                        buf_putc(&para, '\n');
                    if (emo)
                        buf_puts(&para, emo);
                    else
                        buf_printf(&para, ":%s:", code);
                    buf_append(&para, sp, l - (size_t)(sp - r));
                }
                r += l;
                if (*r == '\n')
                    r++;
            }
            if (para.len) {
                buf_clear(&text);
                utf8_to_latin1(&text, buf_str(&para), 1);
                add_line(cv, VL_PILLS, buf_str(&text), (int)text.len);
            }
        }
    }
    buf_free(&text);
    buf_free(&md);
    buf_free(&para);
}

void chat_scroll_to(ChatView *cv, int top)
{
    int max = cv->n - cv->visible;
    if (max < 0)
        max = 0;
    if (top > max)
        top = max;
    if (top < 0)
        top = 0;
    cv->top = top;
    cv->follow = top >= max;
}

static void pill(struct RastPort *rp, int x, int y, int h, const char *s, int len, const ViewPens *p)
{
    int w = TextLength(rp, (CONST_STRPTR)s, (ULONG)len) + 10;
    SetAPen(rp, p->sep);
    RectFill(rp, x, y + 1, x + w - 1, y + h - 2);
    SetAPen(rp, p->pill_bg);
    RectFill(rp, x + 1, y + 2, x + w - 2, y + h - 3);
    SetAPen(rp, p->text);
    SetBPen(rp, p->pill_bg);
    text_at(rp, x + 5, y + 1, s, len);
    SetBPen(rp, p->bg);
}

void chat_draw(ChatView *cv, struct RastPort *rp, const struct IBox *box, const ViewPens *p)
{
    int lh = cv->line_h ? cv->line_h : line_height(rp);
    int indent = text_indent(lh), av = avatar_size(lh);
    int left = box->Left, right = box->Left + box->Width - 1, bottom = box->Top + box->Height - 1;
    int i, y;

    if (box->Width <= 0 || box->Height <= 0)
        return;
    cv->visible = box->Height / lh;
    if (cv->follow)
        cv->top = cv->n - cv->visible;
    chat_scroll_to(cv, cv->top);

    SetDrMd(rp, JAM2);
    SetBPen(rp, p->bg);
    /* backgrounds first, so an avatar can overlap the following line */
    SetAPen(rp, p->bg);
    RectFill(rp, left, box->Top, right, bottom);

    for (i = cv->top, y = box->Top; i < cv->n && y + lh - 1 <= bottom; i++, y += lh) {
        const VLine *l = &cv->v[i];
        const char *s = cv->pool.data ? cv->pool.data + l->off : "";
        int x = left + indent, avail = right - x - PAD;

        switch (l->kind) {
        case VL_DAY: {
            int tw = TextLength(rp, (CONST_STRPTR)s, (ULONG)l->len);
            int cx = left + (box->Width - tw) / 2, my = y + lh / 2;
            SetAPen(rp, p->sep);
            if (cx - 10 > left + PAD)
                RectFill(rp, left + PAD, my, cx - 10, my);
            if (cx + tw + 10 < right - PAD)
                RectFill(rp, cx + tw + 10, my, right - PAD, my);
            SetAPen(rp, p->dim);
            bold(rp, 1);
            text_at(rp, cx, y, s, l->len);
            bold(rp, 0);
            break;
        }
        case VL_HEADER: {
            int ax = left + PAD, ay = y + 1, nw, n;
            char ini[2];
            if (ay + av - 1 > bottom)
                av = bottom - ay + 1;
            SetAPen(rp, p->avatar[l->avatar % 6]);
            RectFill(rp, ax, ay, ax + av - 1, ay + av - 1);
            ini[0] = l->initial ? l->initial : '?';
            SetAPen(rp, p->bg);
            SetBPen(rp, p->avatar[l->avatar % 6]);
            bold(rp, 1);
            text_at(rp, ax + (av - TextLength(rp, (CONST_STRPTR)ini, 1)) / 2, ay + (av - lh) / 2 + 1, ini, 1);
            SetBPen(rp, p->bg);
            SetAPen(rp, p->text);
            n = fit(rp, s, l->name_len, avail);
            text_at(rp, x, y, s, n);
            nw = TextLength(rp, (CONST_STRPTR)s, (ULONG)n);
            bold(rp, 0);
            SetAPen(rp, p->dim);
            if (l->len > l->name_len) {
                int tx = x + nw + 8;
                n = fit(rp, s + l->name_len, l->len - l->name_len, right - tx - PAD);
                text_at(rp, tx, y, s + l->name_len, n);
            }
            av = avatar_size(lh);
            break;
        }
        case VL_TEXT:
            SetAPen(rp, l->hl ? p->highlight : p->text);
            text_at(rp, x, y, s, fit(rp, s, l->len, avail + PAD));
            break;
        case VL_SYS:
            SetAPen(rp, p->dim);
            italic(rp, 1);
            text_at(rp, x, y, s, fit(rp, s, l->len, avail));
            italic(rp, 0);
            break;
        case VL_PILLS: {
            const char *q = s, *end = s + l->len;
            int px = x;
            while (q < end) {
                const char *e = (const char *)memchr(q, '\n', (size_t)(end - q));
                int len = (int)((e ? e : end) - q);
                int w = TextLength(rp, (CONST_STRPTR)q, (ULONG)len) + 10;
                if (px + w > right - PAD)
                    break;
                pill(rp, px, y, lh, q, len, p);
                px += w + 6;
                q = e ? e + 1 : end;
            }
            break;
        }
        default:
            break;
        }
    }
}

/* ---------------------------------------------------------------- sidebar */

void side_init(SideView *sv)
{
    memset(sv, 0, sizeof(*sv));
}

void side_free(SideView *sv)
{
    ar_free(sv->r);
    memset(sv, 0, sizeof(*sv));
}

void side_clear(SideView *sv)
{
    sv->n = 0;
}

SRow *side_add(SideView *sv)
{
    SRow *r;
    if (sv->n == sv->cap) {
        int nc = sv->cap ? sv->cap * 2 : 64;
        SRow *nr = (SRow *)ar_realloc(sv->r, sizeof(SRow) * (size_t)nc);
        if (!nr)
            return NULL;
        sv->r = nr;
        sv->cap = nc;
    }
    r = &sv->r[sv->n++];
    memset(r, 0, sizeof(*r));
    return r;
}

void side_scroll_to(SideView *sv, int top)
{
    int max = sv->n - sv->visible;
    if (max < 0)
        max = 0;
    if (top > max)
        top = max;
    if (top < 0)
        top = 0;
    sv->top = top;
}

int side_hit(const SideView *sv, int y)
{
    int i;
    if (y < 0 || sv->row_h <= 0)
        return -1;
    i = sv->top + y / sv->row_h;
    return i < sv->n ? i : -1;
}

void side_draw(SideView *sv, struct RastPort *rp, const struct IBox *box, const ViewPens *p)
{
    int rh = rp->TxHeight + 5, left = box->Left, right = box->Left + box->Width - 1;
    int bottom = box->Top + box->Height - 1, i, y;

    if (box->Width <= 0 || box->Height <= 0)
        return;
    sv->row_h = rh;
    sv->visible = box->Height / rh;
    side_scroll_to(sv, sv->top);
    SetDrMd(rp, JAM2);
    SetAPen(rp, p->side_bg);
    RectFill(rp, left, box->Top, right, bottom);

    for (i = sv->top, y = box->Top; i < sv->n && y + rh - 1 <= bottom; i++, y += rh) {
        const SRow *r = &sv->r[i];
        int ty = y + 2, x;
        char g[2];

        if (r->kind == SR_GAP)
            continue;
        if (r->kind == SR_SECTION) {
            g[0] = r->collapsed ? '>' : 'v';
            SetBPen(rp, p->side_bg);
            SetAPen(rp, p->side_dim);
            text_at(rp, left + PAD, ty, g, 1);
            SetAPen(rp, p->side_text);
            bold(rp, 1);
            text_at(rp, left + PAD + rp->TxWidth * 2, ty, r->label,
                    fit(rp, r->label, (int)strlen(r->label), right - left - PAD * 3 - rp->TxWidth * 2));
            bold(rp, 0);
            continue;
        }
        /* room row */
        if (r->selected) {
            SetAPen(rp, p->side_sel);
            RectFill(rp, left + 2, y, right - 2, y + rh - 1);
        }
        SetBPen(rp, r->selected ? p->side_sel : p->side_bg);
        x = left + PAD + rp->TxWidth * 2;
        g[0] = (char)r->glyph;
        SetAPen(rp, p->side_dim);
        text_at(rp, x, ty, g, 1);
        x += rp->TxWidth * 2;
        {
            char cnt[12];
            int cw = 0, n;
            if (r->count != 0) {
                if (r->count > 0)
                    snprintf(cnt, sizeof(cnt), "%d", r->count);
                else
                    strcpy(cnt, "!");
                cw = TextLength(rp, (CONST_STRPTR)cnt, (ULONG)strlen(cnt)) + 10;
                SetAPen(rp, p->accent);
                RectFill(rp, right - PAD - cw, y + 1, right - PAD, y + rh - 2);
                SetAPen(rp, p->accent_text);
                SetBPen(rp, p->accent);
                bold(rp, 1);
                text_at(rp, right - PAD - cw + 5, ty, cnt, (int)strlen(cnt));
                bold(rp, 0);
                SetBPen(rp, r->selected ? p->side_sel : p->side_bg);
                cw += 6;
            }
            SetAPen(rp, p->side_text);
            if (r->bold)
                bold(rp, 1);
            n = fit(rp, r->label, (int)strlen(r->label), right - PAD - cw - x);
            text_at(rp, x, ty, r->label, n);
            if (r->bold)
                bold(rp, 0);
        }
    }
    SetBPen(rp, p->bg);
}

/* ------------------------------------------------------------- room header */

void header_draw(struct RastPort *rp, const struct IBox *box, const ViewPens *p,
                 const char *title, const char *topic, int favorite)
{
    int left = box->Left, right = box->Left + box->Width - 1, bottom = box->Top + box->Height - 1;
    int ty = box->Top + (box->Height - rp->TxHeight) / 2 - 1, x = left + PAD + 2, n;

    if (box->Width <= 0 || box->Height <= 0)
        return;
    SetDrMd(rp, JAM2);
    SetAPen(rp, p->bg);
    RectFill(rp, left, box->Top, right, bottom);
    SetAPen(rp, p->sep);
    RectFill(rp, left, bottom, right, bottom);
    SetBPen(rp, p->bg);
    SetAPen(rp, p->text);
    bold(rp, 1);
    n = fit(rp, title, (int)strlen(title), right - x - PAD);
    text_at(rp, x, ty, title, n);
    x += TextLength(rp, (CONST_STRPTR)title, (ULONG)n) + rp->TxWidth;
    bold(rp, 0);
    if (favorite) {
        SetAPen(rp, p->highlight);
        text_at(rp, x, ty, "*", 1);
        x += rp->TxWidth * 2;
    }
    if (topic && *topic && x < right - PAD) {
        SetAPen(rp, p->dim);
        text_at(rp, x, ty, topic, fit(rp, topic, (int)strlen(topic), right - x - PAD));
    }
}

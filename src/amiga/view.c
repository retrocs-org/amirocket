#include "view.h"
#include "../core/charset.h"
#include "../core/emoji.h"
#include "../core/mdtext.h"
#include "../core/mem.h"
#include "../core/timeutil.h"
#include "../core/util.h"

#include <graphics/text.h>
#include <proto/graphics.h>

#include <stdio.h>
#include <string.h>


#define PAD          6
#define GROUP_GAP_MS (5LL * 60000LL)

/* ---------------------------------------------------------------- helpers */

static int line_height(struct RastPort *rp)
{
    return rp->TxHeight + 5;
}

static int avatar_size(int lh)
{
    return lh * 2 - 4;
}

static int text_indent(int lh)
{
    return PAD + avatar_size(lh) + 8;
}

/* ---- emoji-aware text ---- */

static const ViewPens *g_pens;   /* pens for emoji while drawing */

static int esize_index(struct RastPort *rp)
{
    int i, best = 0;
    for (i = 0; i < EMOJI_NSIZES; i++)
        if (emoji_sizes[i] <= rp->TxHeight + 2)
            best = i;
    return best;
}

int view_emoji_size(struct RastPort *rp)
{
    return emoji_sizes[esize_index(rp)];
}

static int is_mark(const char *s, int i, int len)
{
    return (unsigned char)s[i] == EMOJI_MARK && i + 1 < len && (unsigned char)s[i + 1] >= EMOJI_BASE;
}

int view_text_width(struct RastPort *rp, const char *s, int len)
{
    int w = 0, i = 0, ew = view_emoji_size(rp) + 1;
    while (i < len) {
        int j = i;
        if (is_mark(s, i, len)) {
            w += ew;
            i += 2;
            continue;
        }
        while (j < len && !is_mark(s, j, len))
            j++;
        w += TextLength(rp, (CONST_STRPTR)s + i, (ULONG)(j - i));
        i = j;
    }
    return w;
}

void view_emoji(struct RastPort *rp, const ViewPens *p, int x, int y, int idx)
{
    int si = esize_index(rp), sz = emoji_sizes[si], r, c;
    const unsigned char *bm = emoji_bitmap(idx, si);
    if (!bm)
        return;
    for (r = 0; r < sz; r++) {
        const unsigned char *row = bm + r * sz;
        for (c = 0; c < sz;) {
            int v = row[c], e = c;
            while (e < sz && row[e] == v)
                e++;
            if (v) {
                SetAPen(rp, p->emoji[v]);
                RectFill(rp, x + c, y + r, x + e - 1, y + r);
            }
            c = e;
        }
    }
}

void view_text(struct RastPort *rp, const ViewPens *p, int x, int y, int line_h, const char *s, int len)
{
    int i = 0, ew = view_emoji_size(rp) + 1;
    ULONG pen = rp->FgPen;
    while (i < len) {
        int j = i;
        if (is_mark(s, i, len)) {
            if (p)
                view_emoji(rp, p, x, y + (line_h - view_emoji_size(rp)) / 2, (unsigned char)s[i + 1] - EMOJI_BASE);
            SetAPen(rp, pen);
            x += ew;
            i += 2;
            continue;
        }
        while (j < len && !is_mark(s, j, len))
            j++;
        Move(rp, x, y + rp->TxBaseline + 1);
        Text(rp, (CONST_STRPTR)s + i, (ULONG)(j - i));
        x += TextLength(rp, (CONST_STRPTR)s + i, (ULONG)(j - i));
        i = j;
    }
}

static void text_at(struct RastPort *rp, int x, int y, const char *s, int len)
{
    if (len <= 0)
        return;
    view_text(rp, g_pens, x, y, rp->TxHeight + 3, s, len);
}

/* Characters of s that fit in maxw pixels; never splits an emoji. */
static int fit(struct RastPort *rp, const char *s, int len, int maxw)
{
    struct TextExtent te;
    int w = 0, i = 0, ew = view_emoji_size(rp) + 1;

    if (maxw <= 0 || len <= 0)
        return 0;
    while (i < len) {
        int j = i, n;
        if (is_mark(s, i, len)) {
            if (w + ew > maxw)
                return i;
            w += ew;
            i += 2;
            continue;
        }
        while (j < len && !is_mark(s, j, len))
            j++;
        n = (int)TextFit(rp, (CONST_STRPTR)s + i, (ULONG)(j - i), &te, NULL, 1, (ULONG)(maxw - w), 32767);
        if (n < j - i)
            return i + n;
        w += TextLength(rp, (CONST_STRPTR)s + i, (ULONG)(j - i));
        i = j;
    }
    return len;
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
    buf_init(&cv->pool); buf_init(&cv->urls);
    cv->follow = 1;
}

void chat_free(ChatView *cv)
{
    buf_free(&cv->pool); buf_free(&cv->urls); ar_free(cv->links);
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
    l->msg_index = cv->layout_msg;
    l->file_index = -1;
    l->kind = (UBYTE)kind;
    l->off = (LONG)cv->pool.len;
    if (len > 0x7fff)
        len = 0x7fff;
    l->len = (WORD)len;
    if (len > 0)
        buf_append(&cv->pool, text, (size_t)len);
    return l;
}

/* Each wrapped fragment retains the full URL, including long split links. */
static void line_links(ChatView *cv, struct RastPort *rp, const char *paragraph,
                       const char *line, int len)
{
    const char *p = paragraph;
    while (*p) {
        const char *end, *a, *b;
        int n;
        LONG url_off;
        if (ar_strncasecmp(p, "https://", 8) && ar_strncasecmp(p, "http://", 7)) { p++; continue; }
        end = p;
        while (*end && (unsigned char)*end > 32 && *end != '<' && *end != '>' && *end != '"') end++;
        while (end > p) {
            char c = end[-1], open = c == ')' ? '(' : c == ']' ? '[' : c == '}' ? '{' : 0;
            if (strchr(".,;:!?", c)) { end--; continue; }
            if (open) {
                const char *q; int balance = 0;
                for (q = p; q < end; q++) { if (*q == open) balance++; if (*q == c) balance--; }
                if (balance < 0) { end--; continue; }
            }
            break;
        }
        a = p > line ? p : line; b = end < line + len ? end : line + len;
        if (a < b) {
            VLink *l;
            size_t off;
            n = (int)(end - p); url_off = -1;
            for (off = 0; off < cv->urls.len; off += strlen(cv->urls.data + off) + 1)
                if (strlen(cv->urls.data + off) == (size_t)n && !strncmp(cv->urls.data + off, p, n)) { url_off = off; break; }
            if (url_off < 0) {
                url_off = cv->urls.len;
                if (buf_append(&cv->urls, p, n) < 0 || buf_putc(&cv->urls, 0) < 0) return;
            }
            if (cv->nlinks == cv->caplinks) {
                int cap = cv->caplinks ? cv->caplinks * 2 : 32;
                VLink *items = ar_realloc(cv->links, sizeof(*items) * (size_t)cap);
                if (!items) return;
                cv->links = items; cv->caplinks = cap;
            }
            l = &cv->links[cv->nlinks++]; l->row = cv->n - 1;
            l->start = (int)(a - line); l->len = (int)(b - a);
            l->x = (cv->no_avatars ? PAD + 8 : text_indent(cv->line_h)) + view_text_width(rp, line, l->start);
            l->width = view_text_width(rp, a, l->len); l->url = url_off;
        }
        p = end > p ? end : p + 1;
    }
}

/* Word-wraps one paragraph into lines of at most maxw pixels. */
static void wrap(ChatView *cv, struct RastPort *rp, int kind, const char *s, int maxw, int hl)
{
    int len = (int)strlen(s);
    const char *paragraph = s;

    if (len == 0) {
        VLine *l = add_line(cv, kind, "", 0);
        if (l)
            l->hl = (UBYTE)hl;
        return;
    }
    if(kind==VL_CODE) {VLine *l=add_line(cv,kind,s,len);if(l)l->hl=hl;return;}
    while (len > 0) {
        int n = fit(rp, s, len, maxw), cut;
        VLine *l;
        if (kind == VL_CODE) {
            cut = n > 0 ? n : 1;
        } else if (n >= len) {
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
        if (l && kind == VL_TEXT) line_links(cv, rp, paragraph, s, cut);
        s += cut;
        len -= cut;
        while (kind != VL_CODE && len > 0 && *s == ' ') {
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

const char *view_attachment_limit(const RcAttachment *file,int preview)
{
    /* Original file size is not the size of a separate server thumbnail. */
    if(preview && file->image && file->preview_url && *file->preview_url && file->url && strcmp(file->preview_url,file->url)) return NULL;
    if(file->size>8LL*1024*1024) return "This file exceeds the 8 MiB download limit. Open it using the desktop client.";
    if(preview && file->size>2LL*1024*1024) return "This image exceeds the 2 MiB preview limit. Save it instead (up to 8 MiB).";
    return NULL;
}

void view_file_size(char *out,int size,long long bytes)
{
    if(bytes<=0) snprintf(out,size,"size unknown");
    else if(bytes<1024) snprintf(out,size,"%lld B",bytes);
    else if(bytes<1048576) snprintf(out,size,"%lld.%lld KiB",bytes/1024,(bytes%1024)*10/1024);
    else snprintf(out,size,"%lld.%lld MiB",bytes/1048576,(bytes%1048576)*10/1048576);
}

void chat_layout(ChatView *cv, struct RastPort *rp, int width, const RcMsgList *msgs,
                 long long tz_ms, long today, const char *me, const char *empty_text)
{
    int lh = line_height(rp), indent = cv->no_avatars ? PAD + 8 : text_indent(lh), maxw = width - indent - PAD, i;
    long lastday = -1;
    const char *prev_user = NULL;
    long long prev_ts = 0;
    Buf text, md, para;

    buf_clear(&cv->pool); buf_clear(&cv->urls); cv->nlinks = 0;
    cv->n = 0;
    cv->width = width;
    if (cv->code_font && cv->code_font->tf_YSize + 5 > lh) lh = cv->code_font->tf_YSize + 5;
    cv->line_h = lh;
    indent = cv->no_avatars ? PAD + 8 : text_indent(lh); maxw = width - indent - PAD;
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
        cv->layout_msg = i;
        long long local = m->ts_ms + tz_ms;
        long day = ms_day_number(local);
        const char *sys = rc_system_text(m->type);
        char hm[8];
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
        if(cv->unread_id && cv->unread_id[0] && !strcmp(cv->unread_id,m->id)) {
            add_line(cv,VL_UNREAD,"New messages",12); prev_user=NULL;
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
        /* Preserve fenced code separately from prose and emoji conversion. */
        {
            const char *raw = m->text ? m->text : "";
            int fenced = 0;
            char fence = 0;
            do {
                const char *end = strchr(raw, '\n'), *t;
                size_t len = end ? (size_t)(end - raw) : strlen(raw);
                int marker;
                if (len && raw[len - 1] == '\r') len--;
                buf_clear(&para); buf_append(&para, raw, len);
                t = buf_str(&para); while (*t == ' ' || *t == '\t') t++;
                marker = (!strncmp(t, "```", 3) || !strncmp(t, "~~~", 3)) && !strstr(t + 3, !strncmp(t,"```",3) ? "```" : "~~~");
                if (marker && (!fenced || *t == fence)) {
                    fence=*t;
                    fenced = !fenced;
                } else {
                    buf_clear(&text);
                    if (fenced) {
                        struct TextFont *old = rp->Font;
                        Buf expanded; const char *c; int col = 0;
                        buf_init(&expanded);
                        for (c = buf_str(&para); *c; c++) {
                            if (*c == '\t') { do { buf_putc(&expanded, ' '); col++; } while (col % 4); }
                            else { buf_putc(&expanded, *c); col++; }
                        }
                        utf8_to_latin1(&text, buf_str(&expanded), 1); buf_free(&expanded);
                        if (cv->code_font) SetFont(rp, cv->code_font);
                        wrap(cv, rp, VL_CODE, buf_str(&text), maxw, 0);
                        SetFont(rp, old);
                    } else {
                        buf_clear(&md); md_to_plain(&md, buf_str(&para));
                        utf8_to_latin1_ex(&text, buf_str(&md), 1, !cv->no_emoji);
                        wrap(cv, rp, VL_TEXT, buf_str(&text), maxw, hl);
                    }
                }
                if (!end) break;
                raw = end + 1;
            } while (1);
        }
        if (m->edited) wrap(cv, rp, VL_SYS, "(edited)", maxw, 0);

        if (m->file_count) {
            int f;
            for (f = 0; f < m->file_count; f++) {
                int j, state=0; char title[256], size[48], label[320]; VLine *line;
                buf_clear(&para);
                utf8_to_latin1(&para,m->files[f].title?m->files[f].title:"attachment",0);
                view_file_size(size,sizeof(size),m->files[f].size);
                side_label_fit(rp,buf_str(&para),(maxw<480?maxw:480)-view_text_width(rp,size,strlen(size))-view_text_width(rp,"[IMG]   ",8),1,title);
                snprintf(label,sizeof(label),"%s %s  %s",m->files[f].image?"[IMG]":"[FILE]",title,size);
                line=add_line(cv,VL_FILE,label,strlen(label)); if(line)line->file_index=f;
                for(j=0;j<cv->nimages;j++) if(cv->images[j].msg_index==i && cv->images[j].file_index==f) state=cv->images[j].state;
                if(m->files[f].image && cv->inline_images)
                    snprintf(label,sizeof(label),"%s | Open | Save",state==1?"Loading...":state==2?"Failed: Retry":state==3?"Hide":"Preview");
                else strcpy(label,"Open | Save");
                line=add_line(cv,VL_FILE_ACTION,label,strlen(label)); if(line) {line->file_index=f;line->hl=!(m->files[f].image && cv->inline_images);}
                for (j = 0; j < cv->nimages; j++) if (cv->images[j].msg_index == i && cv->images[j].file_index == f) {
                    int y;
                    for (y = 0; y < cv->images[j].height; y += lh) {
                        VLine *line = add_line(cv, VL_IMAGE, "", 0);
                        if (line) { line->file_index = f; line->image_y = y; }
                    }
                }
            }
        } else if (m->n_attachments) {
            buf_clear(&para);
            buf_puts(&para, "[Attachment] ");
            utf8_to_latin1(&para, m->attachment ? m->attachment : "No download link", 0);
            wrap(cv, rp, VL_SYS, buf_str(&para), maxw, 0);
        }

        if (m->reactions) {   /* "code count" lines -> "<3 1\n(y) 2" pills */
            const char *r = m->reactions;ULONG own=0;int reaction_index=0;
            buf_clear(&para);
            while (*r) {
                const char *e = strchr(r, '\n'), *sp;
                char code[64];
                size_t l = e ? (size_t)(e - r) : strlen(r);
                sp = (const char *)memchr(r, ' ', l);
                if (sp && (size_t)(sp - r) < sizeof(code)) {
                    int e;
                    memcpy(code, r, (size_t)(sp - r));
                    code[sp - r] = 0;
                    if(reaction_index<32 && rc_reaction_has(m,code,me))own|=1UL<<reaction_index;
                    reaction_index++;
                    e = emoji_by_code(code, strlen(code));
                    if (para.len)
                        buf_putc(&para, '\n');
                    if (e >= 0 && !cv->no_emoji) {
                        buf_putc(&para, (char)EMOJI_MARK);
                        buf_putc(&para, (char)(EMOJI_BASE + e));
                    } else if (e >= 0) {
                        buf_puts(&para, emoji_table[e].fallback);
                    } else {
                        buf_printf(&para, ":%s:", code);
                    }
                    buf_append(&para, sp, l - (size_t)(sp - r));
                }
                r += l;
                if (*r == '\n')
                    r++;
            }
            if(para.len) {VLine *line=add_line(cv,VL_PILLS,buf_str(&para),(int)para.len);if(line)line->own_reactions=own;}
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
    int w = view_text_width(rp, s, len) + 10;
    SetAPen(rp, p->sep);
    RectFill(rp, x, y + 1, x + w - 1, y + h - 2);
    SetAPen(rp, p->pill_bg);
    RectFill(rp, x + 1, y + 2, x + w - 2, y + h - 3);
    SetAPen(rp, p->pill_text);
    SetBPen(rp, p->pill_bg);
    text_at(rp, x + 5, y + 1, s, len);
    SetBPen(rp, p->bg);
}

/* A bounded lexical colour pass; never changes whitespace or copied text. */
static void code_text(struct RastPort *rp,const ViewPens *p,int x,int y,const char *s,int len,int width)
{
    int i=0,limit=fit(rp,s,len,width);
    while(i<limit) {
        int start=i;LONG pen=p->pill_text;unsigned char c=s[i];
        if(c=='#' || (c=='/' && i+1<limit && s[i+1]=='/') || (c==';' && !i)) {i=limit;pen=p->dim;}
        else if(c=='"' || c=='\'') {int quote=c;i++;while(i<limit) {if(s[i]=='\\' && i+1<limit)i+=2;else if(s[i++]==quote)break;}pen=p->presence[0];}
        else if(c>='0' && c<='9') {do{i++;}while(i<limit && ((s[i]>='0' && s[i]<='9') || s[i]=='.' || s[i]=='x' || (s[i]>='a' && s[i]<='f')));pen=p->presence[2];}
        else if((c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_') {
            char word[48];int n;do{i++;}while(i<limit && ((s[i]>='a' && s[i]<='z') || (s[i]>='A' && s[i]<='Z') || (s[i]>='0' && s[i]<='9') || s[i]=='_'));
            n=i-start;if(n<(int)sizeof(word)-2) {char key[52];memcpy(word,s+start,n);word[n]=0;snprintf(key,sizeof(key),"|%s|",word);
              if(strstr("|echo|printf|if|else|then|fi|for|while|do|done|return|int|char|void|const|struct|def|class|import|from|true|false|null|function|var|let|local|set|",key))pen=p->avatar[0];}
        } else i++;
        if(pen==p->pill_bg)pen=p->pill_text;
        SetAPen(rp,pen);Move(rp,x,y);Text(rp,(CONST_STRPTR)(s+start),i-start);x+=TextLength(rp,(CONST_STRPTR)(s+start),i-start);
    }
}

void chat_draw(ChatView *cv, struct RastPort *rp, const struct IBox *box, const ViewPens *p)
{
    int lh = cv->line_h ? cv->line_h : line_height(rp);
    int indent = cv->no_avatars ? PAD + 8 : text_indent(lh), av = avatar_size(lh);
    int left = box->Left, right = box->Left + box->Width - 1, bottom = box->Top + box->Height - 1;
    int i, y;

    if (box->Width <= 0 || box->Height <= 0)
        return;
    g_pens = p;
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
        case VL_UNREAD:
        case VL_DAY: {
            int tw = view_text_width(rp, s, l->len);
            int cx = left + (box->Width - tw) / 2, my = y + lh / 2;
            SetAPen(rp, l->kind==VL_UNREAD?p->highlight:p->sep);
            if (cx - 10 > left + PAD)
                RectFill(rp, left + PAD, my, cx - 10, my);
            if (cx + tw + 10 < right - PAD)
                RectFill(rp, cx + tw + 10, my, right - PAD, my);
            SetAPen(rp, l->kind==VL_UNREAD?p->highlight:p->dim);
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
            if (!cv->no_avatars) {
            SetAPen(rp, p->avatar[l->avatar % 6]);
            RectFill(rp, ax, ay, ax + av - 1, ay + av - 1);
            ini[0] = l->initial ? l->initial : '?';
            SetAPen(rp, p->bg);
            SetBPen(rp, p->avatar[l->avatar % 6]);
            bold(rp, 1);
            text_at(rp, ax + (av - TextLength(rp, (CONST_STRPTR)ini, 1)) / 2, ay + (av - lh) / 2 + 1, ini, 1);
            SetBPen(rp, p->bg);
            }
            bold(rp, 1);
            SetAPen(rp, p->text);
            n = fit(rp, s, l->name_len, avail);
            text_at(rp, x, y, s, n);
            nw = view_text_width(rp, s, n);
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
        case VL_IMAGE: {
            int j;
            SetAPen(rp,p->pill_bg);RectFill(rp,x-3,y,x+(avail<480?avail:480),y+lh-1);
            for (j = 0; j < cv->nimages; j++) {
                const ChatImage *image = &cv->images[j];
                if (image->msg_index == l->msg_index && image->file_index == l->file_index) {
                    int h = image->height - l->image_y, w = image->width;
                    if (h > lh) h = lh;
                    if (w > avail) w = avail;
                    if (h > 0 && w > 0 && image->bitmap)
                        BltBitMapRastPort(image->bitmap, 0, l->image_y, rp, x, y, w, h, 0xC0);
                }
            }
            break;
        }
        case VL_FILE:
        case VL_FILE_ACTION:
            SetAPen(rp,p->pill_bg);
            RectFill(rp,x-3,y,x+(avail<480?avail:480),y+lh-1);
            SetBPen(rp,p->pill_bg);SetAPen(rp,p->pill_text);
            bold(rp,0);
            if(l->kind==VL_FILE_ACTION)SetSoftStyle(rp,FSF_UNDERLINED,AskSoftStyle(rp));
            text_at(rp,x,y,s,fit(rp,s,l->len,avail));
            SetSoftStyle(rp,0,AskSoftStyle(rp));SetBPen(rp,p->bg);
            break;
        case VL_CODE: {
            struct TextFont *old = rp->Font;
            SetAPen(rp, p->pill_bg);
            RectFill(rp, x - 3, y, right - PAD, y + lh - 1);
            SetBPen(rp, p->pill_bg); SetAPen(rp, p->pill_text);
            if (cv->code_font) SetFont(rp, cv->code_font);
            Move(rp, x, y + (lh - rp->TxHeight) / 2 + rp->TxBaseline);
            code_text(rp,p,x,y+(lh-rp->TxHeight)/2+rp->TxBaseline,s,l->len,avail);
            SetFont(rp, old); SetBPen(rp, p->bg);
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
            int px = x,index=0;
            while (q < end) {
                const char *e = (const char *)memchr(q, '\n', (size_t)(end - q));
                int len = (int)((e ? e : end) - q);
                int w = view_text_width(rp, q, len) + 10;
                if (px + w > right - PAD)
                    break;
                if(index<32 && (l->own_reactions&(1UL<<index))) {ViewPens selected=*p;selected.pill_bg=p->accent;selected.pill_text=p->accent_text;pill(rp,px,y,lh,q,len,&selected);}
                else pill(rp, px, y, lh, q, len, p);
                index++;
                px += w + 6;
                q = e ? e + 1 : end;
            }
            break;
        }
        default:
            break;
        }
    }
    { int j;
      for (j = 0; j < cv->nlinks; j++) {
        const VLink *link = &cv->links[j]; const VLine *l;
        if (link->row < cv->top || link->row >= cv->top + cv->visible) continue;
        l = &cv->v[link->row];
        SetAPen(rp, p->highlight); SetBPen(rp, p->bg);
        SetSoftStyle(rp, FSF_UNDERLINED, AskSoftStyle(rp));
        text_at(rp, left + link->x, box->Top + (link->row - cv->top) * lh,
                cv->pool.data + l->off + link->start, link->len);
        SetSoftStyle(rp, 0, AskSoftStyle(rp));
      }
    }
}

/* Attachment actions use the same text metrics and indent as drawing. */
int chat_reaction_hit(const ChatView *cv,struct RastPort *rp,int x,int y)
{
    int row,index=0,px,right;const VLine *line;const char *q,*end;
    if(y<0 || cv->line_h<=0 || y>=cv->visible*cv->line_h)return -1;
    row=cv->top+y/cv->line_h;if(row<0 || row>=cv->n || cv->v[row].kind!=VL_PILLS)return -1;
    line=&cv->v[row];q=cv->pool.data+line->off;end=q+line->len;
    px=cv->no_avatars?PAD+8:text_indent(cv->line_h);right=cv->width-1-PAD;
    while(q<end) {const char *next=memchr(q,'\n',end-q);int len=(next?next:end)-q,w=view_text_width(rp,q,len)+10;
        if(px+w>right)break;
        if(x>=px && x<px+w)return index;
        px+=w+6;index++;q=next?next+1:end;}
    return -1;
}

int chat_file_action(const ChatView *cv,struct RastPort *rp,int x,int y)
{
    int row,offset,action=0,start=0,i;const VLine *line;const char *s;
    if(y<0 || cv->line_h<=0)return -1;
    row=cv->top+y/cv->line_h;if(row<0 || row>=cv->n)return -1;
    line=&cv->v[row];if(line->kind!=VL_FILE_ACTION)return -1;
    action=line->hl?1:0;
    offset=cv->no_avatars?PAD+8:text_indent(cv->line_h);
    if(x<offset)return -1;
    s=cv->pool.data+line->off;
    for(i=0;i<=line->len;i++)if(i==line->len || s[i]=='|') {
        int end=offset+view_text_width(rp,s,i);
        if(x<end)return action;
        start=i+1;action++;
    }
    (void)start;return -1;
}

const char *chat_link_hit(const ChatView *cv, int x, int y)
{
    int row, i;
    if (y < 0 || cv->line_h <= 0 || y >= cv->visible * cv->line_h) return NULL;
    row = cv->top + y / cv->line_h;
    for (i = 0; i < cv->nlinks; i++) {
        const VLink *l = &cv->links[i];
        if (l->row == row && x >= l->x && x < l->x + l->width)
            return cv->urls.data + l->url;
    }
    return NULL;
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

void side_label_fit(struct RastPort *rp, const char *label, int width, int middle, char out[256])
{
    int len = (int)strlen(label), dots = TextLength(rp, (CONST_STRPTR)"...", 3);
    int prefix, tail = len, budget;
    out[0] = 0;
    if (width <= 0) return;
    if (len < 256 && TextLength(rp, (CONST_STRPTR)label, len) <= width) {
        memcpy(out, label, (size_t)len + 1); return;
    }
    if (width < dots) return;
    budget = width - dots;
    if (middle) {
        while (tail > 0 && len - tail < 180 &&
               TextLength(rp, (CONST_STRPTR)label + tail - 1, len - tail + 1) <= budget * 2 / 3) tail--;
        budget -= TextLength(rp, (CONST_STRPTR)label + tail, len - tail);
    }
    prefix = fit(rp, label, len, budget);
    if (prefix > 252 - (len - tail)) prefix = 252 - (len - tail);
    if (prefix < 0) prefix = 0;
    memcpy(out, label, (size_t)prefix);
    memcpy(out + prefix, "...", 3);
    memcpy(out + prefix + 3, label + tail, (size_t)(len - tail));
    out[prefix + 3 + len - tail] = 0;
}

void side_draw(SideView *sv, struct RastPort *rp, const struct IBox *box, const ViewPens *p)
{
    int rh = rp->TxHeight + 5, icon = rp->TxHeight + 4, left = box->Left, right = box->Left + box->Width - 1;
    int bottom = box->Top + box->Height - 1, i, y;

    if (box->Width <= 0 || box->Height <= 0)
        return;
    g_pens = p;
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

        if (r->kind == SR_GAP) continue;
        if(r->depth) {
            int guide=left+PAD+(r->depth-1)*12+3;
            SetAPen(rp,p->sep); Move(rp,guide,y); Draw(rp,guide,y+rh-1);
            Move(rp,guide,y+rh/2); Draw(rp,guide+6,y+rh/2);
        }
        if (r->kind == SR_SECTION || r->kind == SR_SERVER) {
            g[0] = r->kind==SR_SERVER && r->connection_state!=2 ? (r->connection_state==3?'!':'o') : r->collapsed ? '>' : 'v';
            if(r->selected) { SetAPen(rp,p->side_sel); RectFill(rp,left+2,y,right-2,y+rh-1); }
            SetBPen(rp,r->selected?p->side_sel:p->side_bg);
            SetAPen(rp, p->side_dim);
            text_at(rp, left + PAD + r->depth * 12, ty, g, 1);
            SetAPen(rp, r->kind==SR_SERVER && r->connection_state!=2 ?
                (r->connection_state==3?p->presence[2]:r->connection_state==1?p->accent:p->side_dim) : p->side_text);
            bold(rp, 1);
            text_at(rp, left + PAD + r->depth * 12 + icon + 6, ty, r->label,
                    fit(rp, r->label, (int)strlen(r->label), right - left - PAD * 3 - icon - 6 - r->depth * 12));
            bold(rp, 0);
            continue;
        }
        /* room row */
        if (r->selected) {
            SetAPen(rp, p->side_sel);
            RectFill(rp, left + 2, y, right - 2, y + rh - 1);
        }
        SetBPen(rp, r->selected ? p->side_sel : p->side_bg);
        x = left + PAD + r->depth * 12;
        if (r->participants > 1) {
            char number[8];
            int nw;
            snprintf(number, sizeof(number), "%d", r->participants);
            nw = TextLength(rp, (CONST_STRPTR)number, strlen(number));
            SetAPen(rp, p->accent);
            RectFill(rp, x, y + 1, x + icon - 1, y + rh - 2);
            SetAPen(rp, p->accent_text); SetBPen(rp, p->accent);
            text_at(rp, x + (icon - nw) / 2, ty, number, (int)strlen(number));
            SetBPen(rp, r->selected ? p->side_sel : p->side_bg);
        } else if (r->is_dm) {
            if (sv->no_avatars) {
                SetAPen(rp, r->selected ? p->side_sel_text : p->side_dim);
                text_at(rp, x, ty, "@", 1);
            } else if (r->bitmap) BltBitMapRastPort(r->bitmap, 0, 0, rp, x, y + 1, icon, icon, 0xC0);
            else {
                char initial = r->label[0] ? r->label[0] : '?';
                LONG colour = p->avatar[name_colour(r->label)];
                if (initial >= 'a' && initial <= 'z') initial -= 32;
                SetAPen(rp, colour); RectFill(rp, x, y + 1, x + icon - 1, y + rh - 2);
                SetAPen(rp, p->avatar_text[name_colour(r->label)]); SetBPen(rp, colour);
                text_at(rp, x + (icon - TextLength(rp, (CONST_STRPTR)&initial, 1)) / 2, ty, &initial, 1);
                SetBPen(rp, r->selected ? p->side_sel : p->side_bg);
            }
        } else {
            g[0] = (char)r->glyph;
            SetAPen(rp, r->selected ? p->side_sel_dim : p->side_dim);
            text_at(rp, x + 2, ty, g, 1);
        }
        x += icon + 6;
        if (r->is_dm) {
            int d = rp->TxHeight / 2;
            int cy = y + (rh - d) / 2;
            if (d < 4) d = 4;
            SetAPen(rp, r->presence && !r->participants ? p->presence[(r->presence - 1) % 3] :
                    (r->selected ? p->side_sel_dim : p->side_dim));
            RectFill(rp, x, cy, x + d - 1, cy + d - 1);
            if (!r->presence || r->participants) {
                SetAPen(rp, r->selected ? p->side_sel : p->side_bg);
                RectFill(rp, x + 1, cy + 1, x + d - 2, cy + d - 2);
            }
            x += d + 6;
        }
        {
            char cnt[32];
            int cw = 0;
            char label[256];
            if (r->count != 0 || r->mentions) {
                if(r->mentions && r->count>0) snprintf(cnt,sizeof(cnt),"@%d / %d",r->mentions,r->count);
                else if(r->mentions) snprintf(cnt,sizeof(cnt),"@%d",r->mentions);
                else if (r->count > 0)
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
            SetAPen(rp, r->selected ? p->side_sel_text : p->side_text);
            if (r->bold)
                bold(rp, 1);
            side_label_fit(rp, r->label, right - PAD - cw - x, r->participants > 1, label);
            text_at(rp, x, ty, label, (int)strlen(label));
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
    g_pens = p;
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
    x += view_text_width(rp, title, n) + rp->TxWidth;
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

int chat_file_hit(const ChatView *cv, int y, int *message, int *file)
{
    int row;
    if (y < 0 || cv->line_h <= 0 || y >= cv->visible * cv->line_h) return 0;
    row = cv->top + y / cv->line_h;
    if (row < 0 || row >= cv->n || cv->v[row].file_index < 0) return 0;
    *message = cv->v[row].msg_index; *file = cv->v[row].file_index;
    return 1;
}

void composer_draw(struct RastPort *rp, const struct IBox *box, const ViewPens *p, const char *label)
{
    const char *hint = "Enter sends | Shift+Enter new line";
    int right = box->Left + box->Width - PAD, y = box->Top + (box->Height - rp->TxHeight) / 2;
    int hint_width = TextLength(rp, (CONST_STRPTR)hint, strlen(hint));
    int budget = box->Width - PAD * 2;
    char text[256];
    if (box->Width <= 0 || box->Height <= 0) return;
    SetDrMd(rp, JAM2); SetAPen(rp, p->bg); SetBPen(rp, p->bg);
    RectFill(rp, box->Left, box->Top, box->Left + box->Width - 1, box->Top + box->Height - 1);
    bold(rp, 0);
    if (budget > hint_width + 120) {
        SetAPen(rp, p->dim); text_at(rp, right - hint_width, y, hint, (int)strlen(hint));
        budget -= hint_width + 20;
    }
    SetAPen(rp, p->text); side_label_fit(rp, label, budget, 0, text);
    text_at(rp, box->Left + PAD, y, text, (int)strlen(text));
}

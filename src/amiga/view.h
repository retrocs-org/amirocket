/*
 * AmiRocket - custom-drawn views in the style of the Rocket.Chat client:
 * the room sidebar, the room header and the message view. They render into
 * ReAction space gadgets with the theme's pens; scrolling is by whole lines.
 */
#ifndef AR_VIEW_H
#define AR_VIEW_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>

#include "../core/buf.h"
#include "../core/rc_api.h"

typedef struct {
    LONG bg, text, dim, accent, accent_text, highlight;
    LONG side_bg, side_sel, side_text, side_dim;
    LONG sep, pill_bg;
    LONG avatar[6];
} ViewPens;

/* ---- message view ---- */

typedef struct {
    UBYTE kind;        /* VL_* */
    UBYTE hl;          /* text in the highlight pen (mentions you) */
    UBYTE avatar;      /* header: avatar colour index */
    char  initial;     /* header: avatar letter */
    WORD  name_len;    /* header: bytes of the (bold) name; the time follows */
    LONG  off;         /* text offset in the pool */
    WORD  len;
} VLine;

typedef struct {
    Buf    pool;
    VLine *v;
    int    n, cap;
    int    width;      /* width the layout was made for */
    int    line_h;
    int    top;        /* first visible line */
    int    visible;    /* lines that fit */
    int    follow;     /* keep the newest message in view */
} ChatView;

void chat_init(ChatView *cv);
void chat_free(ChatView *cv);
/* Lays the messages out for a pixel width. me: own user name (mentions). */
void chat_layout(ChatView *cv, struct RastPort *rp, int width, const RcMsgList *msgs,
                 long long tz_ms, long today, const char *me, const char *empty_text);
void chat_draw(ChatView *cv, struct RastPort *rp, const struct IBox *box, const ViewPens *p);
void chat_scroll_to(ChatView *cv, int top);    /* clamps; clears follow unless at end */

/* ---- sidebar ---- */

enum { SR_SECTION, SR_ROOM, SR_GAP };

typedef struct {
    UBYTE kind;
    UBYTE section;     /* section index (SR_SECTION and SR_ROOM) */
    UBYTE collapsed;   /* SR_SECTION */
    UBYTE selected;
    UBYTE bold;        /* unread */
    UBYTE glyph;       /* '#', '*', '@' */
    WORD  count;       /* unread count, 0 = none, -1 = alert without count */
    const char *rid;   /* points into the GUI's room list */
    char  label[96];   /* ISO-8859-1 */
} SRow;

typedef struct {
    SRow *r;
    int   n, cap;
    int   top;
    int   visible;
    int   row_h;
} SideView;

void side_init(SideView *sv);
void side_free(SideView *sv);
void side_clear(SideView *sv);
SRow *side_add(SideView *sv);
void side_draw(SideView *sv, struct RastPort *rp, const struct IBox *box, const ViewPens *p);
int  side_hit(const SideView *sv, int y);      /* row index at y inside the box, or -1 */
void side_scroll_to(SideView *sv, int top);

/* ---- room header ---- */

void header_draw(struct RastPort *rp, const struct IBox *box, const ViewPens *p,
                 const char *title, const char *topic, int favorite);

#endif

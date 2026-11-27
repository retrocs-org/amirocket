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
#include "emoji_data.h"

typedef struct {
    LONG bg, text, dim, accent, accent_text, highlight;
    LONG side_bg, side_sel, side_text, side_dim, side_sel_text, side_sel_dim;
    LONG sep, pill_bg, pill_text;
    LONG avatar[6], avatar_text[6];
    LONG presence[3];
    LONG emoji[EMOJI_COLOURS];   /* [0] unused (transparent) */
} ViewPens;

void view_file_size(char *out,int size,long long bytes);
const char *view_attachment_limit(const RcAttachment *file,int preview);

/* Text drawing that understands emoji markers (see core/emoji.h). */
int  view_text_width(struct RastPort *rp, const char *s, int len);
void view_text(struct RastPort *rp, const ViewPens *p, int x, int y, int line_h, const char *s, int len);
void view_emoji(struct RastPort *rp, const ViewPens *p, int x, int y, int idx);
int  view_emoji_size(struct RastPort *rp);

/* ---- message view ---- */
enum { VL_GAP, VL_DAY, VL_HEADER, VL_TEXT, VL_SYS, VL_PILLS, VL_FILE, VL_CODE, VL_IMAGE, VL_UNREAD, VL_FILE_ACTION };
typedef struct { int row, start, len, x, width; LONG url; } VLink;
typedef struct { int msg_index, file_index, width, height, state; struct BitMap *bitmap; } ChatImage;

typedef struct {
    int msg_index;
    int file_index;    /* -1 except clickable attachment lines */
    UBYTE kind;        /* VL_* */
    UBYTE hl;          /* text in the highlight pen (mentions you) */
    UBYTE avatar;      /* header: avatar colour index */
    char  initial;     /* header: avatar letter */
    ULONG own_reactions;
    WORD image_y;
    WORD  name_len;    /* header: bytes of the (bold) name; the time follows */
    LONG  off;         /* text offset in the pool */
    WORD  len;
} VLine;

typedef struct {
    Buf    pool, urls;
    const char *unread_id; /* borrowed first unread message id */
    VLink *links;
    int nlinks, caplinks;
    int no_emoji, no_avatars, inline_images;
    ChatImage images[8]; int nimages;
    struct TextFont *code_font; /* borrowed; fixed-width code font */
    VLine *v;
    int    n, cap;
    int    layout_msg;
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

/* Visible row -> message and attachment; returns 1 for a downloadable file. */
const char *chat_link_hit(const ChatView *cv, int x, int y);
int chat_reaction_hit(const ChatView *cv,struct RastPort *rp,int x,int y);
int chat_file_action(const ChatView *cv, struct RastPort *rp, int x, int y);
int chat_file_hit(const ChatView *cv, int y, int *message, int *file);

/* ---- sidebar ---- */

enum { SR_SECTION, SR_ROOM, SR_GAP, SR_SERVER };

typedef struct {
    int server_slot; UBYTE depth,connection_state;
    UBYTE kind;
    UBYTE section;     /* section index (SR_SECTION and SR_ROOM) */
    UBYTE collapsed;   /* SR_SECTION */
    UBYTE is_dm, presence; /* 0 unknown/offline, 1 online, 2 away, 3 busy */
    char avatar_user[96];
    struct BitMap *bitmap;  /* borrowed from the GUI avatar cache */
    UBYTE selected;
    UBYTE bold;        /* unread */
    UBYTE glyph;       /* '#', '*', '@' */
    WORD  participants; /* group DM size, including you; 0 for other rows */
    WORD mentions;
    WORD  count;       /* unread count, 0 = none, -1 = alert without count */
    const char *rid;   /* points into the GUI's room list */
    char  label[256];   /* ISO-8859-1 */
} SRow;

typedef struct {
    SRow *r;
    int   n, cap;
    int   top;
    int   visible;
    int   row_h;
    int   no_avatars;
} SideView;

/* Pixel-measured clipping; group labels retain the distinguishing tail. */
void side_label_fit(struct RastPort *rp, const char *label, int width, int middle, char out[256]);
void side_init(SideView *sv);
void side_free(SideView *sv);
void side_clear(SideView *sv);
SRow *side_add(SideView *sv);
void side_draw(SideView *sv, struct RastPort *rp, const struct IBox *box, const ViewPens *p);
int  side_hit(const SideView *sv, int y);      /* row index at y inside the box, or -1 */
void side_scroll_to(SideView *sv, int top);

/* ---- room header ---- */

void composer_draw(struct RastPort *rp, const struct IBox *box, const ViewPens *p, const char *label);
void header_draw(struct RastPort *rp, const struct IBox *box, const ViewPens *p,
                 const char *title, const char *topic, int favorite);

#endif

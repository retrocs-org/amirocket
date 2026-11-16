/*
 * AmiRocket - ReAction user interface (AmigaOS 3.2).
 *
 *  +--------------+------------------------------------------+
 *  | # general (3)| 07:21  alice   Hej på dig                |
 *  | * amiga-dev  | 07:22  tommy   AmiSSL works on 68020     |
 *  | @ alice      |                                          |
 *  |              +------------------------------------------+
 *  |              | [message input                 ] [Send]  |
 *  +--------------+------------------------------------------+
 *  | Connected to chat.example.com as tommy                  |
 *  +---------------------------------------------------------+
 */
#include "gui.h"
#include "prefs.h"
#include "theme.h"
#include "view.h"
#include "emoji_data.h"
#include "../core/emoji.h"
#include "worker.h"
#include "../core/charset.h"
#include "../core/mdtext.h"
#include "../core/mem.h"
#include "../core/timeutil.h"
#include "../core/url.h"
#include "../core/util.h"

#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <intuition/sghooks.h>
#include <devices/inputevent.h>
#include <libraries/gadtools.h>
#include <utility/hooks.h>
#include <graphics/regions.h>
#include <graphics/gfxbase.h>
#include <graphics/gfx.h>
#include <libraries/locale.h>

#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/listbrowser.h>
#include <gadgets/space.h>
#include <gadgets/scroller.h>
#include <gadgets/checkbox.h>
#include <images/label.h>
#include <reaction/reaction_macros.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/utility.h>
#include <proto/locale.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/listbrowser.h>
#include <proto/space.h>
#include <proto/scroller.h>
#include <proto/checkbox.h>
#include <proto/label.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *GadToolsBase;
struct Library *UtilityBase;
struct LocaleBase *LocaleBase;
struct Library *WindowBase, *LayoutBase, *ButtonBase, *StringBase;
struct Library *ListBrowserBase, *CheckBoxBase, *LabelBase, *SpaceBase, *ScrollerBase;

#define MAX_MESSAGES 300
#define INPUT_MAX    4000
#define MAX_SUGG     8

enum {
    G_ROOMS = 1, G_MSGS, G_INPUT, G_SEND, G_STATUS, G_ROOMHDR, G_SIDESCROLL, G_CHATSCROLL,
    L_SERVER, L_USER, L_PASS, L_CODE, L_VERIFY, L_REMEMBER, L_CONNECT, L_CANCEL,
    G_COUNT
};

enum { M_CONNECT = 1, M_DISCONNECT, M_LOGOUT, M_ABOUT, M_QUIT, M_REFRESH, M_OWNSCREEN, M_THEME_BASE = 100 };

static struct NewMenu g_newmenu[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Connect...", (STRPTR)"C", 0, 0, (APTR)M_CONNECT },
    { NM_ITEM, (STRPTR)"Disconnect", (STRPTR)"D", 0, 0, (APTR)M_DISCONNECT },
    { NM_ITEM, (STRPTR)"Log out and forget login", NULL, 0, 0, (APTR)M_LOGOUT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"About...", (STRPTR)"?", 0, 0, (APTR)M_ABOUT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)M_QUIT },
    { NM_TITLE, (STRPTR)"Room", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Check for new messages", (STRPTR)"R", 0, 0, (APTR)M_REFRESH },
    { NM_TITLE, (STRPTR)"Settings", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Theme on its own screen", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)M_OWNSCREEN },
    { NM_ITEM, (STRPTR)"Theme", NULL, 0, 0, NULL },
    /* theme sub-items are appended at run time */
    { NM_END, NULL, NULL, 0, 0, NULL }
};
#define BASE_MENU_ITEMS ((int)(sizeof(g_newmenu) / sizeof(g_newmenu[0])) - 1)

enum { SEC_FAV, SEC_CHAN, SEC_DM, SEC_N };
static const char *g_sec_names[SEC_N] = { "Favorites", "Channels", "Direct messages" };

enum { DIRTY_SIDE = 1, DIRTY_CHAT = 2, DIRTY_HEADER = 4 };

typedef struct {
    struct Screen *scr;
    int own_screen;            /* scr is AmiRocket's themed screen */
    ULONG colors32[2 + 3 * TC_COUNT];
    struct NewMenu *newmenu;
    APTR vi;
    struct Menu *menu;
    UWORD hipen, fillpen;
    struct MsgPort *port;      /* worker replies and events */

    Object *win_obj, *g[G_COUNT];
    struct Window *win;
    Object *lwin_obj, *lroot;
    struct Window *lwin;

    struct List room_list, msg_list;   /* unused, kept for the cleanup path */

    SideView side;
    ChatView chat;
    ViewPens pens;
    LONG     obtained[TC_COUNT + 8 + EMOJI_COLOURS];
    int      n_obtained;
    struct Hook render_hook, idcmp_hook, edit_hook;
    volatile int input_changed;   /* set by the string edit hook */
    char     hook_buf[INPUT_MAX + 1];  /* input text as of the last key */
    volatile int hook_pos;        /* cursor position as of the last key */
    volatile int sugg_move;       /* arrow keys while the panel is open */
    volatile int sugg_close;      /* Esc */
    int      sugg_sel;
    volatile int last_key;        /* last raw key the edit hook saw (shown in the panel) */
    int      click_seen[G_COUNT]; /* click already handled via notification */
    ULONG    last_click_ms[G_COUNT];
    BYTE     dirty_sig;
    ULONG    dirty;            /* DIRTY_* bits */
    int      chat_relayout;
    char     topic[256];       /* UTF-8 */
    char    *members;          /* open room: "username\tname\tstatus\n" lines (UTF-8) */
    struct { char user[96]; char name[96]; int online; int recent; int emoji; char ins[100]; } sugg[MAX_SUGG];
    int      sugg_kind;        /* 0 people (@), 1 emoji (:) */
    int      n_sugg;
    int      sugg_at, sugg_end;  /* '@' position and cursor in the input text */

    Prefs prefs;
    RcRoomList rooms;
    RcMsgList msgs;
    char cur_rid[64];
    char loading_rid[64];
    char username[96];
    char host[256];
    int connected, busy, quit;
    int pending;               /* commands awaiting replies */
    ar_ms tz_ms;               /* local time minus UTC */
    int tz_known;

    char status[256];
    char title[160];
    char roomhdr[200];
    int  collapsed[SEC_N];
    char about[512];
} Gui;

static Gui G;

/* ---- helpers ------------------------------------------------------- */

static void to_latin1(Buf *b, const char *utf8, int multiline)
{
    if (utf8)
        utf8_to_latin1(b, utf8, multiline);
}

static void request(const char *title, const char *text)
{
    struct EasyStruct es;

    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (STRPTR)title;
    es.es_TextFormat = (STRPTR)"%s";
    es.es_GadgetFormat = (STRPTR)"OK";
    EasyRequest(G.lwin ? G.lwin : G.win, &es, NULL, (ULONG)text);
}

/* Error texts come from the server or the network stack as UTF-8. */
static void request_utf8(const char *title, const char *utf8)
{
    Buf b;
    buf_init(&b);
    to_latin1(&b, utf8, 1);
    request(title, buf_str(&b));
    buf_free(&b);
}

static void set_status(const char *latin1)
{
    ar_strlcpy(G.status, latin1, sizeof(G.status));
    if (G.win)
        SetGadgetAttrs((struct Gadget *)G.g[G_STATUS], G.win, NULL, GA_Text, (ULONG)G.status, TAG_END);
}

static void set_status_utf8(const char *utf8)
{
    Buf b;
    buf_init(&b);
    to_latin1(&b, utf8, 0);
    set_status(buf_str(&b));
    buf_free(&b);
}

static void set_busy(int on)
{
    G.busy = on;
    if (G.win_obj)
        SetAttrs(G.win_obj, WA_BusyPointer, on, TAG_END);
    if (G.lwin_obj)
        SetAttrs(G.lwin_obj, WA_BusyPointer, on, TAG_END);
}

static const RcRoom *find_room(const char *rid)
{
    int i;
    for (i = 0; i < G.rooms.count; i++)
        if (!strcmp(G.rooms.items[i].rid, rid))
            return &G.rooms.items[i];
    return NULL;
}

/* "# general", "* priv-board", "@ thebiga", "@ Styrbjorn (you)",
 * "@ RetroFritzl +2" for a group DM. */
static void room_label(Buf *b, const RcRoom *r, int counts)
{
    buf_puts(b, r->type == 'd' ? "@ " : r->type == 'p' ? "* " : "# ");
    if (r->type == 'd') {
        const char *comma = strchr(r->name, ',');
        if (comma) {
            Buf first;
            const char *p;
            int others = 0;
            buf_init(&first);
            buf_append(&first, r->name, (size_t)(comma - r->name));
            for (p = comma; p; p = strchr(p + 1, ','))
                others++;
            to_latin1(b, ar_trim(first.data ? first.data : (char *)""), 0);
            buf_printf(b, " +%d", others);
            buf_free(&first);
        } else {
            to_latin1(b, r->name, 0);
            if (G.username[0] && !ar_strcasecmp(r->name, G.username))
                buf_puts(b, " (you)");
        }
    } else {
        to_latin1(b, rc_room_title(r), 0);
    }
    if (counts) {
        if (r->unread > 0)
            buf_printf(b, "  (%d)", r->unread);
        else if (r->alert)
            buf_puts(b, "  (!)");
    }
}

static void mark_dirty(ULONG what);

static void update_title(void)
{
    const RcRoom *r = G.cur_rid[0] ? find_room(G.cur_rid) : NULL;
    Buf b;

    buf_init(&b);
    buf_puts(&b, "AmiRocket");
    if (r) {
        buf_puts(&b, " - ");
        room_label(&b, r, 0);
    }
    ar_strlcpy(G.title, buf_str(&b), sizeof(G.title));
    buf_clear(&b);
    if (r) {
        room_label(&b, r, 0);
        if (r->type == 'd' && r->fname && *r->fname && strcmp(r->fname, r->name) && !strchr(r->name, ',')) {
            buf_puts(&b, "   ");
            to_latin1(&b, r->fname, 0);
        } else if (r->type == 'd' && strchr(r->name, ',')) {
            buf_puts(&b, "   ");
            to_latin1(&b, r->name, 0);
        }
    } else {
        buf_puts(&b, G.connected ? "Select a room" : "Not connected");
    }
    ar_strlcpy(G.roomhdr, buf_str(&b), sizeof(G.roomhdr));
    buf_free(&b);
    if (G.win)
        SetWindowTitles(G.win, (STRPTR)G.title, (STRPTR)~0);
    mark_dirty(DIRTY_HEADER);
}

/* Local wall-clock time from the Amiga clock (local since 1978). */
static ar_ms amiga_local_ms(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ((ar_ms)(2922 + ds.ds_Days) * 86400000LL) + (ar_ms)ds.ds_Minute * 60000LL + (ar_ms)ds.ds_Tick * 20LL;
}

/* Amiga clock is local time since 1978-01-01; derive the offset to UTC
 * from the server's Date header. Falls back to the locale setting. */
static void update_tz(ar_ms server_now)
{
    struct DateStamp ds;
    ar_ms local, diff, q = 15LL * 60000LL;

    if (server_now <= 0) {
        if (!G.tz_known && LocaleBase) {
            struct Locale *loc = OpenLocale(NULL);
            if (loc) {
                G.tz_ms = -(ar_ms)loc->loc_GMTOffset * 60000LL;
                CloseLocale(loc);
            }
        }
        return;
    }
    DateStamp(&ds);
    /* 2922 = days from 1970-01-01 to 1978-01-01 */
    local = ((ar_ms)(2922 + ds.ds_Days) * 86400000LL) + (ar_ms)ds.ds_Minute * 60000LL + (ar_ms)ds.ds_Tick * 20LL;
    diff = local - server_now;
    G.tz_ms = ((diff + (diff >= 0 ? q / 2 : -q / 2)) / q) * q;
    G.tz_known = 1;
}

static void cmd_open_room(const char *rid);

/* ---- views --------------------------------------------------------- */

static void free_list(struct List *l)
{
    struct Node *n;
    while ((n = RemHead(l)) != NULL)
        FreeListBrowserNode(n);
}

static int room_section(const RcRoom *r)
{
    if (r->favorite)
        return SEC_FAV;
    return r->type == 'd' ? SEC_DM : SEC_CHAN;
}

static const RcRoomList *g_sort_rooms;

static int cmp_room_idx(const void *a, const void *b)
{
    return rc_room_compare(&g_sort_rooms->items[*(const int *)a], &g_sort_rooms->items[*(const int *)b]);
}

static void mark_dirty(ULONG what)
{
    G.dirty |= what;
}

static int area_box(Object *o, struct IBox *out)
{
    struct IBox *b = NULL;
    if (!G.win || !o)
        return 0;
    GetAttr(SPACE_AreaBox, o, (ULONG *)&b);
    if (!b || b->Width <= 0 || b->Height <= 0)
        return 0;
    *out = *b;
    return 1;
}

static void set_scroller(int id, int top, int visible, int total)
{
    if (!G.win)
        return;
    SetGadgetAttrs((struct Gadget *)G.g[id], G.win, NULL,
                   SCROLLER_Total, (ULONG)(total > 0 ? total : 1),
                   SCROLLER_Visible, (ULONG)(visible > 0 ? visible : 1),
                   SCROLLER_Top, (ULONG)top, TAG_END);
}

static void draw_side(void)
{
    struct IBox box;
    if (!area_box(G.g[G_ROOMS], &box))
        return;
    side_draw(&G.side, G.win->RPort, &box, &G.pens);
    set_scroller(G_SIDESCROLL, G.side.top, G.side.visible, G.side.n);
}

static int sugg_row_h(void)
{
    return G.win->RPort->TxHeight + 6;
}

/* "People" panel over the bottom of the message view, like the desktop
 * client's mention popup. */
static void draw_suggestions(const struct IBox *box)
{
    struct RastPort *rp = G.win->RPort;
    const ViewPens *p = &G.pens;
    int rh = sugg_row_h(), rows = G.n_sugg + 1, i;
    int top = box->Top + box->Height - rows * rh - 2, left = box->Left + 4, right = box->Left + box->Width - 5;
    int base = rp->TxBaseline + 3;

    if (top < box->Top)
        top = box->Top;
    SetDrMd(rp, JAM2);
    SetAPen(rp, p->sep);
    RectFill(rp, left, top, right, box->Top + box->Height - 1);
    SetAPen(rp, p->side_bg);
    RectFill(rp, left + 1, top + 1, right - 1, box->Top + box->Height - 2);
    SetBPen(rp, p->side_bg);
    SetAPen(rp, p->dim);
    SetSoftStyle(rp, FSF_BOLD, AskSoftStyle(rp));
    Move(rp, left + 8, top + base);
    if (G.sugg_kind == 1)
        Text(rp, (CONST_STRPTR)"Emoji", 5);
    else
        Text(rp, (CONST_STRPTR)"People", 6);
    SetSoftStyle(rp, 0, AskSoftStyle(rp));
    {
        char hint[80];
        int hl, hw;
        snprintf(hint, sizeof(hint), "Up/Down select  Return insert  Esc close   [key $%02X]", (unsigned)(G.last_key & 0xFF));
        hl = (int)strlen(hint);
        hw = TextLength(rp, (CONST_STRPTR)hint, (ULONG)hl);
        if (right - 8 - hw > left + 8 + TextLength(rp, (CONST_STRPTR)"People", 6) + 16) {
            Move(rp, right - 8 - hw, top + base);
            Text(rp, (CONST_STRPTR)hint, (ULONG)hl);
        }
    }
    for (i = 0; i < G.n_sugg; i++) {
        int y = top + (i + 1) * rh, x = left + 8, dot = rp->TxHeight / 2;
        Buf b;
        if (y + rh > box->Top + box->Height)
            break;
        if (i == G.sugg_sel) {
            SetAPen(rp, p->side_sel);
            RectFill(rp, left + 1, y, right - 1, y + rh - 1);
        }
        SetBPen(rp, i == G.sugg_sel ? p->side_sel : p->side_bg);
        if (G.sugg_kind == 1) {
            int es = view_emoji_size(rp);
            Buf c;
            view_emoji(rp, p, x, y + (rh - es) / 2, G.sugg[i].emoji);
            buf_init(&c);
            buf_printf(&c, ":%s:", G.sugg[i].user);
            SetAPen(rp, p->text);
            SetSoftStyle(rp, FSF_BOLD, AskSoftStyle(rp));
            Move(rp, x + es + 8, y + base);
            Text(rp, (CONST_STRPTR)buf_str(&c), (ULONG)c.len);
            SetSoftStyle(rp, 0, AskSoftStyle(rp));
            buf_free(&c);
            continue;
        }
        /* presence dot */
        SetAPen(rp, G.sugg[i].online ? p->avatar[2] : p->dim);
        RectFill(rp, x, y + (rh - dot) / 2, x + dot - 1, y + (rh - dot) / 2 + dot - 1);
        if (!G.sugg[i].online) {
            SetAPen(rp, i == G.sugg_sel ? p->side_sel : p->side_bg);
            RectFill(rp, x + 1, y + (rh - dot) / 2 + 1, x + dot - 2, y + (rh - dot) / 2 + dot - 2);
        }
        x += dot + 8;
        buf_init(&b);
        to_latin1(&b, G.sugg[i].user, 0);
        SetAPen(rp, p->text);
        SetSoftStyle(rp, FSF_BOLD, AskSoftStyle(rp));
        Move(rp, x, y + base);
        Text(rp, (CONST_STRPTR)buf_str(&b), (ULONG)b.len);
        x += TextLength(rp, (CONST_STRPTR)buf_str(&b), (ULONG)b.len) + 8;
        SetSoftStyle(rp, 0, AskSoftStyle(rp));
        if (G.sugg[i].name[0] && ar_strcasecmp(G.sugg[i].name, G.sugg[i].user)) {
            buf_clear(&b);
            buf_putc(&b, '(');
            to_latin1(&b, G.sugg[i].name, 0);
            buf_putc(&b, ')');
            SetAPen(rp, p->dim);
            Move(rp, x, y + base);
            Text(rp, (CONST_STRPTR)buf_str(&b), (ULONG)b.len);
        }
        if (G.sugg[i].recent) {
            static const char rec[] = "recent";
            int w = TextLength(rp, (CONST_STRPTR)rec, 6);
            SetAPen(rp, p->dim);
            Move(rp, right - 8 - w, y + base);
            Text(rp, (CONST_STRPTR)rec, 6);
        }
        buf_free(&b);
    }
    SetSoftStyle(rp, 0, AskSoftStyle(rp));
    SetBPen(rp, p->bg);
}

static void draw_chat(void)
{
    struct IBox box;
    if (!area_box(G.g[G_MSGS], &box))
        return;
    if (G.chat_relayout || G.chat.width != box.Width) {
        ar_ms now = amiga_local_ms();
        const char *empty = !G.connected ? "Not connected"
                          : !G.cur_rid[0] ? "Select a room"
                          : G.loading_rid[0] ? "Loading messages..." : "No messages yet";
        chat_layout(&G.chat, G.win->RPort, box.Width, &G.msgs, G.tz_ms, ms_day_number(now),
                    G.username, empty);
        G.chat_relayout = 0;
    }
    chat_draw(&G.chat, G.win->RPort, &box, &G.pens);
    set_scroller(G_CHATSCROLL, G.chat.top, G.chat.visible, G.chat.n);
    if (G.n_sugg)
        draw_suggestions(&box);
}

static void draw_header(void)
{
    struct IBox box;
    const RcRoom *r = G.cur_rid[0] ? find_room(G.cur_rid) : NULL;
    Buf t, tp;

    if (!area_box(G.g[G_ROOMHDR], &box))
        return;
    buf_init(&t);
    buf_init(&tp);
    if (r) {
        room_label(&t, r, 0);
        to_latin1(&tp, G.topic, 0);
    } else {
        buf_puts(&t, G.connected ? "Select a room" : "AmiRocket");
    }
    header_draw(G.win->RPort, &box, &G.pens, buf_str(&t), buf_str(&tp), r ? r->favorite : 0);
    buf_free(&t);
    buf_free(&tp);
}

static void redraw_dirty(void)
{
    ULONG d = G.dirty;
    G.dirty = 0;
    if (!G.win)
        return;
    if (d & DIRTY_SIDE)
        draw_side();
    if (d & DIRTY_HEADER)
        draw_header();
    if (d & DIRTY_CHAT)
        draw_chat();
}

/* Called by space.gadget whenever it renders (window refresh, resize):
 * defer to the main loop, which draws with our own data. */
static ULONG render_hook_func(struct Hook *h, Object *o, APTR msg)
{
    G.dirty |= o == G.g[G_ROOMS] ? DIRTY_SIDE : o == G.g[G_MSGS] ? DIRTY_CHAT : DIRTY_HEADER;
    if (G.dirty_sig >= 0)
        Signal((struct Task *)G.port->mp_SigTask, 1UL << G.dirty_sig);
    return 0;
}

static void scroll_side(int top)
{
    side_scroll_to(&G.side, top);
    draw_side();
}

static void scroll_chat(int top)
{
    chat_scroll_to(&G.chat, top);
    draw_chat();
}

static int over(Object *o, WORD mx, WORD my)
{
    struct IBox b;
    return area_box(o, &b) && mx >= b.Left && mx < b.Left + b.Width && my >= b.Top && my < b.Top + b.Height;
}

static void update_suggestions(void);
static void redraw_dirty(void);
static void on_side_click(void);
static void on_chat_click(void);

static int is_name_char(unsigned char c);

/* Runs inside the edit hook: replaces the "@pre" word before the cursor in
 * the work buffer with the selected suggestion (already Latin-1, with a
 * trailing space) and puts the cursor after it. */
static int hook_complete(struct SGWork *sgw)
{
    char *wb = (char *)sgw->WorkBuffer;
    const char *ins;
    int pos, at, n, il, tail, max;

    if (!wb || G.sugg_sel < 0 || G.sugg_sel >= G.n_sugg)
        return 0;
    ins = G.sugg[G.sugg_sel].ins;
    il = (int)strlen(ins);
    n = sgw->NumChars;
    pos = sgw->BufferPos > n ? n : sgw->BufferPos;
    at = pos;
    while (at > 0 && is_name_char((unsigned char)wb[at - 1]))
        at--;
    if (at == 0 || (wb[at - 1] != '@' && wb[at - 1] != ':') || !il)
        return 0;
    at--;                          /* include the '@' or ':' */
    tail = n - pos;
    max = (sgw->StringInfo ? sgw->StringInfo->MaxChars : INPUT_MAX + 1) - 1;
    if (at + il + tail > max)
        return 0;
    memmove(wb + at + il, wb + pos, (size_t)tail);
    memcpy(wb + at, ins, (size_t)il);
    sgw->NumChars = at + il + tail;
    wb[sgw->NumChars] = 0;
    sgw->BufferPos = at + il;
    sgw->EditOp = EO_BIGCHANGE;
    return 1;
}

/* String edit hook on the input line. Intuition calls it for every key
 * (in input.device's context), after its own editing: only note what
 * happened and wake the main loop, which reads the text itself. */
static ULONG edit_hook_func(struct Hook *h, struct SGWork *sgw, ULONG *msg)
{
    if (*msg != SGH_KEY)
        return 0;
    /* string.gadget cannot report the cursor position (STRINGA_BufferPos
     * is not gettable on OS 3.x), so keep our own copy of text and cursor */
    if (sgw->WorkBuffer) {
        int n = sgw->NumChars;
        if (n < 0)
            n = 0;
        if (n > INPUT_MAX)
            n = INPUT_MAX;
        memcpy(G.hook_buf, sgw->WorkBuffer, (size_t)n);
        G.hook_buf[n] = 0;
        G.hook_pos = sgw->BufferPos > n ? n : sgw->BufferPos;
    }
    if (sgw->IEvent && sgw->IEvent->ie_Class == IECLASS_RAWKEY)
        G.last_key = sgw->IEvent->ie_Code;
    if (sgw->IEvent && sgw->IEvent->ie_Class == IECLASS_RAWKEY && G.n_sugg &&
        !(sgw->IEvent->ie_Code & IECODE_UP_PREFIX)) {
        UWORD code = sgw->IEvent->ie_Code;
        int n = G.n_sugg;
        if (code == 0x4C || code == 0x4D) {          /* cursor up / down */
            G.sugg_sel = (G.sugg_sel + (code == 0x4C ? n - 1 : 1)) % n;
            sgw->Actions &= ~(SGA_END | SGA_BEEP);
        } else if (code == 0x45) {                   /* Esc */
            G.sugg_close = 1;
            sgw->Actions &= ~(SGA_END | SGA_BEEP);
        } else if (code == 0x44 || code == 0x43 || code == 0x42) {  /* Return, Enter, Tab */
            /* insert the name right here, so the field stays active with
             * the cursor after "@name " instead of ending and coming back */
            if (hook_complete(sgw))
                sgw->Actions = (sgw->Actions & ~(SGA_END | SGA_BEEP | SGA_NEXTACTIVE | SGA_PREVACTIVE)) |
                               SGA_USE | SGA_REDISPLAY;
            G.n_sugg = 0;
            G.sugg_close = 1;
        }
        if (sgw->WorkBuffer) {
            int k = sgw->NumChars < 0 ? 0 : sgw->NumChars > INPUT_MAX ? INPUT_MAX : sgw->NumChars;
            memcpy(G.hook_buf, sgw->WorkBuffer, (size_t)k);
            G.hook_buf[k] = 0;
            G.hook_pos = sgw->BufferPos > k ? k : sgw->BufferPos;
        }
    }
    G.input_changed = 1;
    if (G.dirty_sig >= 0)
        Signal((struct Task *)G.port->mp_SigTask, 1UL << G.dirty_sig);
    return ~0UL;
}

static void input_events(void)
{
    if (G.sugg_close) {
        G.sugg_close = 0;
        G.sugg_move = 0;
        G.input_changed = 0;
        G.n_sugg = 0;
        mark_dirty(DIRTY_CHAT);
        return;
    }
    if (G.input_changed) {
        G.input_changed = 0;
        G.sugg_move = 0;
        update_suggestions();
        if (G.n_sugg)
            mark_dirty(DIRTY_CHAT);   /* the hook may have moved the selection */
    }
}

/* IDCMP hook: live scroller dragging and the mouse wheel. Runs in our task
 * from inside RA_HandleInput(). */
static ULONG idcmp_hook_func(struct Hook *h, APTR obj, struct IntuiMessage *im)
{
    if (im->Class == IDCMP_IDCMPUPDATE) {
        struct TagItem *tags = (struct TagItem *)im->IAddress;
        struct TagItem *ti = FindTagItem(GA_ID, tags), *top = FindTagItem(SCROLLER_Top, tags);
        if (ti && (ti->ti_Data == G_ROOMS || ti->ti_Data == G_MSGS) && FindTagItem(SPACE_MouseY, tags)) {
            /* space.gadget reports clicks as notifications with the mouse
             * position; it may repeat them while the button is held, so act
             * once per 300 ms */
            ULONG now = im->Seconds * 1000UL + im->Micros / 1000UL;
            G.click_seen[ti->ti_Data] = 1;
            if (now - G.last_click_ms[ti->ti_Data] > 300) {
                G.last_click_ms[ti->ti_Data] = now;
                if (ti->ti_Data == G_ROOMS)
                    on_side_click();
                else
                    on_chat_click();
                redraw_dirty();
            }
        } else if (ti && ti->ti_Data == G_INPUT) {
            update_suggestions();
            redraw_dirty();
        } else if (ti && top) {
            if (ti->ti_Data == G_SIDESCROLL)
                scroll_side((int)top->ti_Data);
            else if (ti->ti_Data == G_CHATSCROLL)
                scroll_chat((int)top->ti_Data);
        }
    } else if (im->Class == IDCMP_EXTENDEDMOUSE && (im->Code & IMSGCODE_INTUIWHEELDATA)) {
        struct IntuiWheelData *wd = (struct IntuiWheelData *)im->IAddress;
        int dy = wd->WheelY * 3;
        if (over(G.g[G_ROOMS], im->MouseX, im->MouseY))
            scroll_side(G.side.top + dy);
        else
            scroll_chat(G.chat.top + dy);
    }
    return 0;
}

static void rooms_rebuild(void)
{
    int *order, i, s;
    Buf b;

    side_clear(&G.side);
    order = (int *)ar_malloc(sizeof(int) * (size_t)(G.rooms.count + 1));
    buf_init(&b);
    for (s = 0; order && s < SEC_N; s++) {
        int cnt = 0;
        SRow *row;
        for (i = 0; i < G.rooms.count; i++) {
            const RcRoom *r = &G.rooms.items[i];
            /* rooms hidden in Rocket.Chat ("open": false) stay hidden, unless
             * open here; the server re-opens them when a message arrives */
            if (!r->open && strcmp(r->rid, G.cur_rid) != 0)
                continue;
            if (room_section(r) == s)
                order[cnt++] = i;
        }
        if (!cnt)
            continue;
        g_sort_rooms = &G.rooms;
        qsort(order, (size_t)cnt, sizeof(int), cmp_room_idx);
        if (G.side.n && (row = side_add(&G.side)) != NULL)
            row->kind = SR_GAP;
        if ((row = side_add(&G.side)) != NULL) {
            row->kind = SR_SECTION;
            row->section = (UBYTE)s;
            row->collapsed = (UBYTE)G.collapsed[s];
            ar_strlcpy(row->label, g_sec_names[s], sizeof(row->label));
        }
        if (G.collapsed[s])
            continue;
        for (i = 0; i < cnt; i++) {
            const RcRoom *r = &G.rooms.items[order[i]];
            const char *lab;
            if (!(row = side_add(&G.side)))
                break;
            row->kind = SR_ROOM;
            row->section = (UBYTE)s;
            row->rid = r->rid;
            row->glyph = r->type == 'd' ? '@' : r->type == 'p' ? '*' : '#';
            row->selected = !strcmp(r->rid, G.cur_rid);
            row->bold = r->unread > 0 || r->alert;
            row->count = (WORD)(r->unread > 0 ? (r->unread > 999 ? 999 : r->unread) : r->alert ? -1 : 0);
            buf_clear(&b);
            room_label(&b, r, 0);
            lab = buf_str(&b);
            if (b.len > 2)
                lab += 2;   /* the glyph is drawn separately */
            ar_strlcpy(row->label, lab, sizeof(row->label));
        }
    }
    buf_free(&b);
    ar_free(order);
    mark_dirty(DIRTY_SIDE | DIRTY_HEADER);
}

static void msgs_rebuild(int scroll_bottom)
{
    G.chat_relayout = 1;
    if (scroll_bottom)
        G.chat.follow = 1;
    mark_dirty(DIRTY_CHAT);
}

static int is_name_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-' || c >= 0xC0;
}

static void add_sugg(const char *user, const char *name, int online, int recent, const char *prefix)
{
    int i;
    size_t pl = strlen(prefix);

    if (G.n_sugg >= MAX_SUGG || !user || !*user)
        return;
    if (pl && ar_strncasecmp(user, prefix, pl) != 0 && (!name || ar_strncasecmp(name, prefix, pl) != 0))
        return;
    for (i = 0; i < G.n_sugg; i++)
        if (!strcmp(G.sugg[i].user, user)) {
            if (online)
                G.sugg[i].online = 1;
            return;
        }
    ar_strlcpy(G.sugg[G.n_sugg].user, user, sizeof(G.sugg[0].user));
    ar_strlcpy(G.sugg[G.n_sugg].name, name ? name : "", sizeof(G.sugg[0].name));
    G.sugg[G.n_sugg].online = online;
    G.sugg[G.n_sugg].recent = recent;
    G.n_sugg++;
}

/* What Return inserts for each suggestion, ready in Latin-1 for the edit
 * hook (which runs in input.device and must not allocate). */
static void fill_ins(void)
{
    int k;
    Buf u;

    buf_init(&u);
    for (k = 0; k < G.n_sugg; k++) {
        buf_clear(&u);
        utf8_to_latin1(&u, G.sugg[k].user, 0);
        snprintf(G.sugg[k].ins, sizeof(G.sugg[0].ins), G.sugg_kind == 1 ? ":%s: " : "@%s ", buf_str(&u));
    }
    buf_free(&u);
}

static void hide_suggestions(void)
{
    if (G.n_sugg) {
        G.n_sugg = 0;
        mark_dirty(DIRTY_CHAT);
    }
}

/* Looks at the word under the cursor: "@pre" opens the People panel. */
static void update_suggestions(void)
{
    STRPTR txt = NULL;
    ULONG pos = 0;
    int at, i, old = G.n_sugg;
    Buf pre;

    if (!G.win || !G.connected || !G.cur_rid[0]) {
        hide_suggestions();
        return;
    }
    txt = (STRPTR)G.hook_buf;
    pos = (ULONG)G.hook_pos;
    if (pos > strlen((char *)txt))
        pos = (ULONG)strlen((char *)txt);
    at = (int)pos;
    while (at > 0 && is_name_char((unsigned char)txt[at - 1]))
        at--;
    if (at == 0 || (txt[at - 1] != '@' && txt[at - 1] != ':') || (at >= 2 && txt[at - 2] != ' ')) {
        hide_suggestions();
        return;
    }
    G.sugg_at = at - 1;
    G.sugg_end = (int)pos;
    if (txt[at - 1] == ':') {   /* :smi -> emoji by shortcode */
        char prefix[40];
        int idx[MAX_SUGG], n, k;
        size_t pl = pos - (ULONG)at;
        if (pl < 2 || pl >= sizeof(prefix)) {
            hide_suggestions();
            return;
        }
        memcpy(prefix, txt + at, pl);
        prefix[pl] = 0;
        n = emoji_complete(prefix, idx, MAX_SUGG);
        G.sugg_kind = 1;
        G.n_sugg = 0;
        for (k = 0; k < n; k++) {
            ar_strlcpy(G.sugg[k].user, emoji_table[idx[k]].code, sizeof(G.sugg[0].user));
            G.sugg[k].name[0] = 0;
            G.sugg[k].online = G.sugg[k].recent = 0;
            G.sugg[k].emoji = idx[k];
            G.n_sugg++;
        }
        if (G.sugg_sel >= G.n_sugg || G.n_sugg != old)
            G.sugg_sel = 0;
        fill_ins();
        if (G.n_sugg || old)
            mark_dirty(DIRTY_CHAT);
        return;
    }
    G.sugg_kind = 0;
    buf_init(&pre);
    buf_append(&pre, (const char *)txt + at, (size_t)(pos - (ULONG)at));
    {   /* the prefix as UTF-8 for matching */
        Buf u;
        buf_init(&u);
        latin1_to_utf8(&u, buf_str(&pre));
        buf_free(&pre);
        pre = u;
    }

    G.n_sugg = 0;
    /* 1. recent senders, newest first */
    for (i = G.msgs.count - 1; i >= 0 && G.n_sugg < MAX_SUGG; i--) {
        const RcMsg *m = &G.msgs.items[i];
        if (!m->type && strcmp(m->username, G.username) != 0)
            add_sugg(m->username, m->name, 0, 1, buf_str(&pre));
    }
    /* 2. other room members */
    if (G.members) {
        const char *l = G.members;
        while (*l && G.n_sugg < MAX_SUGG) {
            const char *e = strchr(l, '\n'), *t1, *t2;
            char user[96], name[96];
            size_t n;
            if (!e)
                e = l + strlen(l);
            t1 = (const char *)memchr(l, '\t', (size_t)(e - l));
            t2 = t1 ? (const char *)memchr(t1 + 1, '\t', (size_t)(e - t1 - 1)) : NULL;
            if (t1 && t2) {
                n = (size_t)(t1 - l) < sizeof(user) ? (size_t)(t1 - l) : sizeof(user) - 1;
                memcpy(user, l, n);
                user[n] = 0;
                n = (size_t)(t2 - t1 - 1) < sizeof(name) ? (size_t)(t2 - t1 - 1) : sizeof(name) - 1;
                memcpy(name, t1 + 1, n);
                name[n] = 0;
                add_sugg(user, name, (size_t)(e - t2 - 1) == 6 && !strncmp(t2 + 1, "online", 6), 0, buf_str(&pre));
            }
            l = *e ? e + 1 : e;
        }
    }
    /* 3. @here / @all for channels */
    add_sugg("here", "notify active users", 0, 0, buf_str(&pre));
    add_sugg("all", "notify everyone", 0, 0, buf_str(&pre));
    buf_free(&pre);
    if (G.sugg_sel >= G.n_sugg)
        G.sugg_sel = 0;
    if (G.n_sugg != old)
        G.sugg_sel = 0;
    fill_ins();
    if (G.n_sugg || old)
        mark_dirty(DIRTY_CHAT);
}

/* Replaces "@pre" in the input with "@username ". */
static void complete_suggestion(int i)
{
    STRPTR txt = NULL;
    Buf b, u;
    int len;

    if (i < 0 || i >= G.n_sugg)
        return;
    txt = (STRPTR)G.hook_buf;
    len = (int)strlen((char *)txt);
    if (G.sugg_at < 0 || G.sugg_at > len || G.sugg_end > len || G.sugg_end < G.sugg_at)
        return;
    buf_init(&b);
    buf_init(&u);
    utf8_to_latin1(&u, G.sugg[i].user, 0);
    buf_append(&b, txt, (size_t)G.sugg_at);
    if (G.sugg_kind == 1) {
        buf_putc(&b, ':');
        buf_puts(&b, buf_str(&u));
        buf_puts(&b, ": ");
    } else {
        buf_putc(&b, '@');
        buf_puts(&b, buf_str(&u));
        buf_putc(&b, ' ');
    }
    {
        int cursor = (int)b.len;
        buf_puts(&b, (const char *)txt + G.sugg_end);
        SetGadgetAttrs((struct Gadget *)G.g[G_INPUT], G.win, NULL,
                       STRINGA_TextVal, (ULONG)buf_str(&b),
                       STRINGA_BufferPos, (ULONG)cursor, TAG_END);
        ar_strlcpy(G.hook_buf, buf_str(&b), sizeof(G.hook_buf));
        G.hook_pos = cursor;
    }
    buf_free(&b);
    buf_free(&u);
    hide_suggestions();
    ActivateLayoutGadget((struct Gadget *)G.g[0], G.win, NULL, (ULONG)G.g[G_INPUT]);
    /* activating may mark the whole text on OS 3.2; setting the cursor
     * again drops the mark and leaves it after "@name " */
    SetGadgetAttrs((struct Gadget *)G.g[G_INPUT], G.win, NULL,
                   STRINGA_BufferPos, (ULONG)G.hook_pos, TAG_END);
}

static void on_chat_click(void)
{
    struct IBox box;
    int rh, rows, top, i;

    if (!G.n_sugg || !area_box(G.g[G_MSGS], &box))
        return;
    rh = sugg_row_h();
    rows = G.n_sugg + 1;
    top = box.Top + box.Height - rows * rh - 2;
    i = (G.win->MouseY - top) / rh - 1;
    if (G.win->MouseY >= top && i >= 0 && i < G.n_sugg)
        complete_suggestion(i);
    else
        hide_suggestions();
}

static void on_side_click(void)
{
    struct IBox box;
    int i;

    if (!area_box(G.g[G_ROOMS], &box))
        return;
    i = side_hit(&G.side, G.win->MouseY - box.Top);
    if (i < 0)
        return;
    if (G.side.r[i].kind == SR_SECTION) {
        int s = G.side.r[i].section;
        G.collapsed[s] = !G.collapsed[s];
        rooms_rebuild();
        return;
    }
    if (G.side.r[i].kind != SR_ROOM || !G.side.r[i].rid || !strcmp(G.side.r[i].rid, G.cur_rid))
        return;
    ar_strlcpy(G.cur_rid, G.side.r[i].rid, sizeof(G.cur_rid));
    G.topic[0] = 0;
    ar_free(G.members);
    G.members = NULL;
    G.n_sugg = 0;
    rc_msglist_free(&G.msgs);
    cmd_open_room(G.cur_rid);
    rooms_rebuild();
    msgs_rebuild(1);
    update_title();
    ActivateLayoutGadget((struct Gadget *)G.g[0], G.win, NULL, (ULONG)G.g[G_INPUT]);
}

/* ---- commands ------------------------------------------------------ */

static void send_cmd(NetMsg *m)
{
    G.pending++;
    worker_send(m);
}

static void cmd_connect_token(void)
{
    NetMsg *m = netmsg_new(NC_CONNECT);
    if (!m)
        return;
    m->a_url = ar_strdup(G.prefs.server);
    m->a_uid = ar_strdup(G.prefs.user_id);
    m->a_token = ar_strdup(G.prefs.token);
    m->a_verify = G.prefs.verify;
    set_status("Connecting with saved login...");
    set_busy(1);
    send_cmd(m);
}

static void cmd_open_room(const char *rid)
{
    NetMsg *m = netmsg_new(NC_OPEN_ROOM);
    if (!m)
        return;
    m->a_rid = ar_strdup(rid);
    ar_strlcpy(G.loading_rid, rid, sizeof(G.loading_rid));
    set_status("Loading messages...");
    send_cmd(m);
}

static void cmd_simple(int kind)
{
    NetMsg *m = netmsg_new(kind);
    if (m)
        send_cmd(m);
}

static void cmd_send_input(void)
{
    STRPTR txt = NULL;
    NetMsg *m;
    Buf b;
    char *t;

    GetAttr(STRINGA_TextVal, G.g[G_INPUT], (ULONG *)&txt);
    if (!txt || !*txt)
        return;
    if (!G.connected || !G.cur_rid[0]) {
        set_status("Select a room first");
        return;
    }
    buf_init(&b);
    latin1_to_utf8(&b, (const char *)txt);
    t = buf_detach(&b);
    ar_trim(t);
    if (!*t) {
        ar_free(t);
        return;
    }
    m = netmsg_new(NC_SEND);
    if (!m) {
        ar_free(t);
        return;
    }
    m->a_rid = ar_strdup(G.cur_rid);
    m->a_text = t;
    send_cmd(m);
    SetGadgetAttrs((struct Gadget *)G.g[G_INPUT], G.win, NULL, STRINGA_TextVal, (ULONG)"", TAG_END);
    G.hook_buf[0] = 0;
    G.hook_pos = 0;
    hide_suggestions();
    ActivateLayoutGadget((struct Gadget *)G.g[0], G.win, NULL, (ULONG)G.g[G_INPUT]);
}

/* ---- login window -------------------------------------------------- */

static Object *mk_string(int id, const char *init, int maxchars, int password)
{
    return StringObject,
        GA_ID, id,
        GA_RelVerify, TRUE,
        GA_TabCycle, TRUE,
        STRINGA_MaxChars, maxchars,
        STRINGA_TextVal, (ULONG)(init ? init : ""),
        password ? STRINGA_HookType : TAG_IGNORE, SHK_PASSWORD,
    End;
}

static void login_close(void)
{
    if (G.lwin_obj) {
        DisposeObject(G.lwin_obj);
        G.lwin_obj = NULL;
        G.lwin = NULL;
        G.lroot = NULL;
    }
}

static void login_open(const char *hint)
{
    Object *root;

    if (G.lwin_obj) {
        WindowToFront(G.lwin);
        ActivateWindow(G.lwin);
        return;
    }
    root = G.lroot = VLayoutObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddChild, LabelObject, LABEL_Text, (ULONG)(hint ? hint : "Log in to a Rocket.Chat server"), End,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, G.g[L_SERVER] = mk_string(L_SERVER, G.prefs.server, 250, 0),
        CHILD_Label, LabelObject, LABEL_Text, (ULONG)"_Server", End,
        LAYOUT_AddChild, G.g[L_USER] = mk_string(L_USER, G.prefs.user, 90, 0),
        CHILD_Label, LabelObject, LABEL_Text, (ULONG)"_User", End,
        LAYOUT_AddChild, G.g[L_PASS] = mk_string(L_PASS, "", 120, 1),
        CHILD_Label, LabelObject, LABEL_Text, (ULONG)"_Password", End,
        LAYOUT_AddChild, G.g[L_CODE] = mk_string(L_CODE, "", 12, 0),
        CHILD_Label, LabelObject, LABEL_Text, (ULONG)"2FA _code", End,
        LAYOUT_AddChild, G.g[L_VERIFY] = CheckBoxObject,
            GA_ID, L_VERIFY, GA_RelVerify, TRUE,
            GA_Text, (ULONG)"_Verify server certificate",
            GA_Selected, G.prefs.verify,
        End,
        LAYOUT_AddChild, G.g[L_REMEMBER] = CheckBoxObject,
            GA_ID, L_REMEMBER, GA_RelVerify, TRUE,
            GA_Text, (ULONG)"_Remember login on this Amiga",
            GA_Selected, G.prefs.remember,
        End,
        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_EvenSize, TRUE,
            LAYOUT_AddChild, G.g[L_CONNECT] = ButtonObject,
                GA_ID, L_CONNECT, GA_RelVerify, TRUE, GA_Text, (ULONG)"C_onnect",
            End,
            LAYOUT_AddChild, G.g[L_CANCEL] = ButtonObject,
                GA_ID, L_CANCEL, GA_RelVerify, TRUE, GA_Text, (ULONG)"C_ancel",
            End,
        End,
        CHILD_WeightedHeight, 0,
    End;
    if (!root)
        return;
    G.lwin_obj = WindowObject,
        WA_Title, (ULONG)"AmiRocket - Connect",
        WA_PubScreen, (ULONG)G.scr,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_Activate, TRUE,
        WA_Width, 380,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_ParentGroup, (ULONG)root,
    End;
    if (!G.lwin_obj) {
        DisposeObject(root);
        return;
    }
    G.lwin = RA_OpenWindow(G.lwin_obj);
    if (!G.lwin) {
        login_close();
        return;
    }
    ActivateLayoutGadget((struct Gadget *)root, G.lwin, NULL,
                         (ULONG)(G.prefs.server[0] && G.prefs.user[0] ? G.g[L_PASS] : G.g[L_SERVER]));
}

static const char *str_of(Object *o)
{
    STRPTR s = NULL;
    GetAttr(STRINGA_TextVal, o, (ULONG *)&s);
    return s ? (const char *)s : "";
}

static int checked(Object *o)
{
    ULONG v = 0;
    GetAttr(GA_Selected, o, &v);
    return v != 0;
}

static void login_submit(void)
{
    NetMsg *m;
    Buf b;
    char server[256], user[96];
    ServerUrl su;
    char err[96];

    ar_strlcpy(server, str_of(G.g[L_SERVER]), sizeof(server));
    ar_strlcpy(user, str_of(G.g[L_USER]), sizeof(user));
    ar_trim(server);
    ar_trim(user);
    if (url_parse_server(server, &su, err, sizeof(err)) < 0) {
        request("AmiRocket", err);
        return;
    }
    if (!user[0] || !*str_of(G.g[L_PASS])) {
        request("AmiRocket", "Please enter your user name and password.");
        return;
    }
    ar_strlcpy(G.prefs.server, server, sizeof(G.prefs.server));
    ar_strlcpy(G.prefs.user, user, sizeof(G.prefs.user));
    G.prefs.verify = checked(G.g[L_VERIFY]);
    G.prefs.remember = checked(G.g[L_REMEMBER]);

    m = netmsg_new(NC_CONNECT);
    if (!m)
        return;
    m->a_url = ar_strdup(server);
    buf_init(&b);
    latin1_to_utf8(&b, user);
    m->a_user = buf_detach(&b);
    latin1_to_utf8(&b, str_of(G.g[L_PASS]));
    m->a_pass = buf_detach(&b);
    m->a_code = ar_strdup(str_of(G.g[L_CODE]));
    ar_trim(m->a_code);
    m->a_verify = G.prefs.verify;
    ar_strlcpy(G.host, su.host, sizeof(G.host));
    set_status("Connecting...");
    set_busy(1);
    send_cmd(m);
}

/* ---- worker results ------------------------------------------------ */

static void forget_login(void)
{
    G.prefs.token[0] = 0;
    G.prefs.user_id[0] = 0;
    prefs_save(&G.prefs);
}

static void went_offline(const char *why)
{
    G.connected = 0;
    rc_roomlist_free(&G.rooms);
    rc_msglist_free(&G.msgs);
    G.cur_rid[0] = 0;
    rooms_rebuild();
    msgs_rebuild(0);
    update_title();
    set_status(why);
}

static void on_connected(NetMsg *m)
{
    char s[256];
    ServerUrl su;
    char err[64];

    set_busy(0);
    if (m->rc != RC_OK) {
        if (m->rc == RC_ERR_TOTP) {
            if (!G.lwin)
                login_open("Two-factor code required");
            set_status("Enter the two-factor code and connect again");
            if (G.lwin)
                ActivateLayoutGadget((struct Gadget *)G.lroot, G.lwin, NULL, (ULONG)G.g[L_CODE]);
            request("AmiRocket",
                    !strcmp(m->totp_method, "email")
                        ? "This account needs a two-factor code.\nThe server has sent one by e-mail."
                        : "This account needs a two-factor code.\nEnter the code from your authenticator app.");
            return;
        }
        if (m->a_token && *m->a_token) {   /* saved token rejected */
            if (m->rc == RC_ERR_AUTH)
                forget_login();
            set_status("Saved login failed");
            login_open(NULL);
        }
        set_status_utf8(m->text[0] ? m->text : rc_strerror(m->rc));
        request_utf8("AmiRocket - connection failed", m->text[0] ? m->text : rc_strerror(m->rc));
        return;
    }
    G.connected = 1;
    ar_strlcpy(G.username, m->username, sizeof(G.username));
    if (url_parse_server(G.prefs.server, &su, err, sizeof(err)) == 0)
        ar_strlcpy(G.host, su.host, sizeof(G.host));
    if (G.prefs.remember) {
        ar_strlcpy(G.prefs.user_id, m->user_id, sizeof(G.prefs.user_id));
        ar_strlcpy(G.prefs.token, m->token, sizeof(G.prefs.token));
    } else {
        G.prefs.user_id[0] = G.prefs.token[0] = 0;
    }
    if (!G.prefs.user[0])
        ar_strlcpy(G.prefs.user, m->username, sizeof(G.prefs.user));
    prefs_save(&G.prefs);
    login_close();
    update_tz(m->server_now);

    rc_roomlist_free(&G.rooms);
    G.rooms = m->rooms;
    rc_roomlist_init(&m->rooms);
    rooms_rebuild();
    snprintf(s, sizeof(s), "Connected to %s as %s%s%s", G.host, m->username,
             m->server_version[0] ? " - Rocket.Chat " : "", m->server_version);
    set_status_utf8(s);

    /* reopen the last room, or the first one */
    if (G.cur_rid[0] && find_room(G.cur_rid))
        cmd_open_room(G.cur_rid);
    else if (G.rooms.count > 0) {
        ar_strlcpy(G.cur_rid, G.rooms.items[0].rid, sizeof(G.cur_rid));
        rooms_rebuild();
        cmd_open_room(G.cur_rid);
    }
    update_title();
}

static void on_room_opened(NetMsg *m)
{
    if (strcmp(m->rid, G.cur_rid) != 0)
        return;   /* user already moved on */
    G.loading_rid[0] = 0;
    if (m->rc != RC_OK) {
        set_status_utf8(m->text[0] ? m->text : rc_strerror(m->rc));
        return;
    }
    rc_msglist_free(&G.msgs);
    rc_msglist_merge(&G.msgs, &m->msgs, MAX_MESSAGES);
    ar_strlcpy(G.topic, m->topic, sizeof(G.topic));
    ar_free(G.members);
    G.members = m->members;
    m->members = NULL;
    update_tz(m->server_now);
    msgs_rebuild(1);
    mark_dirty(DIRTY_HEADER);
    {
        char s[64];
        snprintf(s, sizeof(s), "%d messages", G.msgs.count);
        set_status(s);
    }
    {   /* opening marks the room read on the server; show that right away */
        int i;
        for (i = 0; i < G.rooms.count; i++)
            if (!strcmp(G.rooms.items[i].rid, G.cur_rid)) {
                G.rooms.items[i].unread = 0;
                G.rooms.items[i].alert = 0;
            }
        rooms_rebuild();
    }
}

static void on_sent(NetMsg *m)
{
    if (m->rc != RC_OK) {
        request_utf8("AmiRocket - message not sent", m->text[0] ? m->text : rc_strerror(m->rc));
        return;
    }
    if (!strcmp(m->rid, G.cur_rid) && m->msgs.count) {
        rc_msglist_merge(&G.msgs, &m->msgs, MAX_MESSAGES);
        msgs_rebuild(1);
    }
}

static void on_update(NetMsg *m)
{
    if (m->rooms.count || m->rooms.n_removed) {
        int i;
        for (i = 0; i < m->rooms.n_removed; i++)
            if (!strcmp(m->rooms.removed[i], G.cur_rid)) {
                G.cur_rid[0] = 0;
                rc_msglist_free(&G.msgs);
                msgs_rebuild(0);
                update_title();
            }
        rc_roomlist_merge(&G.rooms, &m->rooms);
        /* the open room is read as far as we are concerned */
        for (i = 0; i < G.rooms.count; i++)
            if (!strcmp(G.rooms.items[i].rid, G.cur_rid)) {
                G.rooms.items[i].unread = 0;
                G.rooms.items[i].alert = 0;
            }
        rooms_rebuild();
    }
    if ((m->msgs.count || m->msgs.n_deleted) && !strcmp(m->rid, G.cur_rid)) {
        rc_msglist_merge(&G.msgs, &m->msgs, MAX_MESSAGES);
        msgs_rebuild(G.chat.follow);
        if (G.win && !(G.win->Flags & WFLG_WINDOWACTIVE))
            DisplayBeep(G.scr);
    }
    update_tz(m->server_now);
}

static void on_status(NetMsg *m)
{
    if (m->rc == RC_ERR_AUTH) {
        forget_login();
        went_offline("Logged out");
        request_utf8("AmiRocket", m->text);
        login_open(NULL);
        return;
    }
    set_status_utf8(m->text);
}

static void handle_port(void)
{
    struct Message *msg;

    while ((msg = GetMsg(G.port)) != NULL) {
        NetMsg *m = (NetMsg *)msg;
        if (msg->mn_Node.ln_Type == NT_REPLYMSG) {
            G.pending--;
            switch (m->kind) {
            case NC_CONNECT:   on_connected(m); break;
            case NC_OPEN_ROOM: on_room_opened(m); break;
            case NC_SEND:      on_sent(m); break;
            case NC_LOGOUT:    went_offline("Logged out"); break;
            case NC_DISCONNECT: went_offline("Disconnected"); break;
            }
            netmsg_free(m);
        } else {
            switch (m->kind) {
            case NE_UPDATE: on_update(m); break;
            case NE_STATUS: on_status(m); break;
            }
            worker_done(m);
        }
    }
}

/* ---- main window --------------------------------------------------- */

static void about(void)
{
    snprintf(G.about, sizeof(G.about),
             "AmiRocket " AR_VERSION "\n"
             "A Rocket.Chat client for AmigaOS 3.2\n\n"
             "Networking: bsdsocket.library, TLS: AmiSSL\n"
             "Messages are fetched every %d seconds.", G.prefs.interval);
    request("About AmiRocket", G.about);
}

static void apply_theme(int idx);

static void handle_menu(UWORD code)
{
    int theme = -1, force_reopen = 0;

    while (code != MENUNULL) {
        struct MenuItem *item = ItemAddress(G.menu, code);
        if (!item)
            break;
        switch ((ULONG)GTMENUITEM_USERDATA(item)) {
        case M_CONNECT:    login_open(NULL); break;
        case M_DISCONNECT: if (G.connected) cmd_simple(NC_DISCONNECT); break;
        case M_LOGOUT:
            forget_login();
            if (G.connected)
                cmd_simple(NC_LOGOUT);
            break;
        case M_ABOUT:      about(); break;
        case M_QUIT:       G.quit = 1; break;
        case M_REFRESH:    if (G.connected) cmd_simple(NC_POLL_NOW); break;
        case M_OWNSCREEN:
            G.prefs.ownscreen = (item->Flags & CHECKED) != 0;
            theme = theme_find(G.prefs.theme);
            force_reopen = 1;
            break;
        default:
            if ((ULONG)GTMENUITEM_USERDATA(item) >= M_THEME_BASE)
                theme = (int)((ULONG)GTMENUITEM_USERDATA(item) - M_THEME_BASE);
            break;
        }
        code = item->NextSelect;
    }
    /* after the loop: switching themes frees the menu strip */
    if (theme >= 0 && (force_reopen || theme != theme_find(G.prefs.theme)))
        apply_theme(theme);
}

static void handle_main(void)
{
    ULONG result;
    UWORD code;

    while ((result = RA_HandleInput(G.win_obj, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            G.quit = 1;
            break;
        case WMHI_MENUPICK:
            handle_menu(code);
            break;
        case WMHI_NEWSIZE:
            G.chat_relayout = 1;
            mark_dirty(DIRTY_SIDE | DIRTY_CHAT | DIRTY_HEADER);
            break;
        case WMHI_RAWKEY:
            /* NewMouse wheel codes, for mouse drivers without V47 wheel events */
            if ((result & WMHI_KEYMASK) == 0x7A)
                scroll_chat(G.chat.top - 3);
            else if ((result & WMHI_KEYMASK) == 0x7B)
                scroll_chat(G.chat.top + 3);
            break;
        case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
            case G_ROOMS:
                if (G.click_seen[G_ROOMS])
                    G.click_seen[G_ROOMS] = 0;
                else
                    on_side_click();
                break;
            case G_SIDESCROLL: {
                ULONG top = 0;
                GetAttr(SCROLLER_Top, G.g[G_SIDESCROLL], &top);
                scroll_side((int)top);
                break;
            }
            case G_CHATSCROLL: {
                ULONG top = 0;
                GetAttr(SCROLLER_Top, G.g[G_CHATSCROLL], &top);
                scroll_chat((int)top);
                break;
            }
            case G_INPUT:
                if (G.n_sugg)
                    complete_suggestion(G.sugg_sel);   /* Return/Tab (any key code): take the selection */
                else if (code != 9 && code != 0x5F)   /* not Tab / Help */
                    cmd_send_input();
                break;
            case G_MSGS:
                if (G.click_seen[G_MSGS])
                    G.click_seen[G_MSGS] = 0;
                else
                    on_chat_click();
                break;
            case G_SEND:
                cmd_send_input();
                break;
            }
            break;
        }
    }
}

static void handle_login(void)
{
    ULONG result;
    UWORD code;
    int close = 0;

    while (G.lwin_obj && (result = RA_HandleInput(G.lwin_obj, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            close = 1;
            break;
        case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
            case L_CONNECT:
                if (!G.busy)
                    login_submit();
                break;
            case L_PASS:
            case L_CODE:
                if (code != 9 && !G.busy)
                    login_submit();
                break;
            case L_CANCEL:
                close = 1;
                break;
            }
            break;
        }
    }
    if (close)
        login_close();
}

static int open_libs(void)
{
    static const struct { struct Library **base; const char *name; ULONG ver; } libs[] = {
        { (struct Library **)&IntuitionBase, "intuition.library", 39 },
        { (struct Library **)&GfxBase, "graphics.library", 39 },
        { &GadToolsBase, "gadtools.library", 39 },
        { &UtilityBase, "utility.library", 39 },
        { &WindowBase, "window.class", 44 },
        { &LayoutBase, "gadgets/layout.gadget", 44 },
        { &ButtonBase, "gadgets/button.gadget", 44 },
        { &StringBase, "gadgets/string.gadget", 44 },
        { &ListBrowserBase, "gadgets/listbrowser.gadget", 44 },
        { &CheckBoxBase, "gadgets/checkbox.gadget", 44 },
        { &LabelBase, "images/label.image", 44 },
        { &SpaceBase, "gadgets/space.gadget", 44 },
        { &ScrollerBase, "gadgets/scroller.gadget", 44 },
        { NULL, NULL, 0 }
    };
    int i;

    for (i = 0; libs[i].base; i++) {
        *libs[i].base = OpenLibrary((CONST_STRPTR)libs[i].name, libs[i].ver);
        if (!*libs[i].base) {
            char b[160];
            snprintf(b, sizeof(b), "AmiRocket needs %s version %lu (AmigaOS 3.2 ReAction).\n",
                     libs[i].name, (unsigned long)libs[i].ver);
            if (IntuitionBase) {
                struct EasyStruct es = { sizeof(struct EasyStruct), 0, (STRPTR)"AmiRocket",
                                         (STRPTR)"%s", (STRPTR)"OK" };
                EasyRequest(NULL, &es, NULL, (ULONG)b);
            } else {
                PutStr((CONST_STRPTR)b);
            }
            return -1;
        }
    }
    LocaleBase = (struct LocaleBase *)OpenLibrary((CONST_STRPTR)"locale.library", 38);
    return 0;
}

static void close_libs(void)
{
    struct Library **all[] = {
        &ScrollerBase, &SpaceBase, &LabelBase, &CheckBoxBase, &ListBrowserBase, &StringBase, &ButtonBase, &LayoutBase,
        &WindowBase, &UtilityBase, &GadToolsBase, (struct Library **)&GfxBase,
        (struct Library **)&LocaleBase, (struct Library **)&IntuitionBase, NULL
    };
    int i;
    for (i = 0; all[i]; i++)
        if (*all[i]) {
            CloseLibrary(*all[i]);
            *all[i] = NULL;
        }
}

/* Opens the themed screen (or locks Workbench for the "workbench" theme). */
static int open_screen(void)
{
    const Theme *t = theme_get(theme_find(G.prefs.theme));

    G.own_screen = 0;
    if (!t->workbench && G.prefs.ownscreen) {
        struct Screen *wb = LockPubScreen((CONST_STRPTR)"Workbench");
        ULONG depth = 4;
        if (wb) {
            depth = GetBitMapAttr(wb->RastPort.BitMap, BMA_DEPTH);
            UnlockPubScreen(NULL, wb);
        }
        /* 16 theme colours need depth 4; RTG truecolour gets an 8-bit screen */
        depth = depth > 8 ? 8 : depth < 4 ? 4 : depth;
        theme_colors32(t, G.colors32);
        G.scr = OpenScreenTags(NULL,
                               SA_LikeWorkbench, TRUE,
                               SA_Depth, depth,
                               SA_Title, (ULONG)"AmiRocket " AR_VERSION,
                               SA_PubName, (ULONG)"AMIROCKET",
                               SA_Pens, (ULONG)theme_pens(),
                               SA_Colors32, (ULONG)G.colors32,
                               SA_SharePens, TRUE,
                               SA_Interleaved, TRUE,
                               SA_ShowTitle, TRUE,
                               TAG_END);
        if (G.scr) {
            PubScreenStatus(G.scr, 0);   /* make it public for requesters */
            G.own_screen = 1;
            return 0;
        }
        /* not enough memory or no such mode: fall back to Workbench */
        ar_strlcpy(G.status, "Could not open the theme screen; using Workbench", sizeof(G.status));
    }
    G.scr = LockPubScreen(NULL);
    return G.scr ? 0 : -1;
}

static void close_screen(void)
{
    if (!G.scr)
        return;
    if (G.own_screen) {
        PubScreenStatus(G.scr, PSNF_PRIVATE);
        while (!CloseScreen(G.scr)) {
            /* another program has a window on our screen */
            struct EasyStruct es = { sizeof(struct EasyStruct), 0, (STRPTR)"AmiRocket",
                                     (STRPTR)"Please close the other windows on the AmiRocket screen.",
                                     (STRPTR)"Retry" };
            EasyRequest(NULL, &es, NULL, TAG_END);
        }
    } else {
        UnlockPubScreen(NULL, G.scr);
    }
    G.scr = NULL;
    G.own_screen = 0;
}

static void build_menu(void)
{
    int n = theme_count(), cur = theme_find(G.prefs.theme), i, k;
    LONG all = (1L << n) - 1;

    G.newmenu = (struct NewMenu *)ar_calloc((size_t)(BASE_MENU_ITEMS + n + 1), sizeof(struct NewMenu));
    if (!G.newmenu)
        return;
    memcpy(G.newmenu, g_newmenu, sizeof(struct NewMenu) * (size_t)BASE_MENU_ITEMS);
    for (i = 0; i < BASE_MENU_ITEMS; i++)
        if ((ULONG)G.newmenu[i].nm_UserData == M_OWNSCREEN && G.prefs.ownscreen)
            G.newmenu[i].nm_Flags |= CHECKED;
    for (i = 0, k = BASE_MENU_ITEMS; i < n; i++, k++) {
        const Theme *t = theme_get(i);
        G.newmenu[k].nm_Type = NM_SUB;
        G.newmenu[k].nm_Label = (STRPTR)t->name;
        G.newmenu[k].nm_Flags = CHECKIT | (i == cur ? CHECKED : 0);
        G.newmenu[k].nm_MutualExclude = all & ~(1L << i);
        G.newmenu[k].nm_UserData = (APTR)(ULONG)(M_THEME_BASE + i);
    }
    G.newmenu[k].nm_Type = NM_END;
    G.menu = CreateMenus(G.newmenu, GTMN_FrontPen, 0, TAG_END);
    if (G.menu && G.vi)
        LayoutMenus(G.menu, G.vi, GTMN_NewLookMenus, TRUE, TAG_END);
}

/* Theme colours as pens on the screen in use: exact on the theme's own
 * screen, nearest free/shared colours on Workbench. */
static LONG pen_rgb(ULONG rgb)
{
    struct ColorMap *cm = G.scr->ViewPort.ColorMap;
    LONG pen = ObtainBestPen(cm, ((rgb >> 16) & 0xFF) * 0x01010101UL, ((rgb >> 8) & 0xFF) * 0x01010101UL,
                             (rgb & 0xFF) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_END);
    if (pen >= 0 && G.n_obtained < (int)(sizeof(G.obtained) / sizeof(G.obtained[0])))
        G.obtained[G.n_obtained++] = pen;
    return pen >= 0 ? pen : 1;
}

static void alloc_emoji_pens(void)
{
    int i;
    G.pens.emoji[0] = G.pens.bg;
    for (i = 1; i < EMOJI_COLOURS; i++)
        G.pens.emoji[i] = pen_rgb(emoji_palette[i]);
}

static void alloc_pens(void)
{
    const Theme *t = theme_get(theme_find(G.prefs.theme));
    ViewPens *p = &G.pens;

    G.n_obtained = 0;
    if (t->workbench) {
        struct DrawInfo *dri = GetScreenDrawInfo(G.scr);
        UWORD *dp = dri ? dri->dri_Pens : NULL;
        p->bg = dp ? dp[BACKGROUNDPEN] : 0;
        p->text = dp ? dp[TEXTPEN] : 1;
        p->dim = dp ? dp[FILLPEN] : 3;
        p->accent = dp ? dp[FILLPEN] : 3;
        p->accent_text = dp ? dp[FILLTEXTPEN] : 2;
        p->highlight = dp ? dp[HIGHLIGHTTEXTPEN] : 2;
        p->side_bg = p->bg;
        p->side_sel = dp ? dp[SHINEPEN] : 2;
        p->side_text = p->text;
        p->side_dim = p->dim;
        p->sep = dp ? dp[SHADOWPEN] : 1;
        p->pill_bg = p->side_sel;
        if (dri)
            FreeScreenDrawInfo(G.scr, dri);
        p->avatar[0] = pen_rgb(0x4A7FD4);
        p->avatar[1] = pen_rgb(0xD45A4A);
        p->avatar[2] = pen_rgb(0x4AA65A);
        p->avatar[3] = pen_rgb(0xD49A3A);
        p->avatar[4] = pen_rgb(0x8A5AC4);
        p->avatar[5] = pen_rgb(0x3AA6A6);
        alloc_emoji_pens();
        return;
    }
    p->bg = pen_rgb(t->rgb[TC_BG]);
    p->text = pen_rgb(t->rgb[TC_TEXT]);
    p->dim = pen_rgb(t->rgb[TC_EXTRA2]);
    p->accent = pen_rgb(t->rgb[TC_FILL]);
    p->accent_text = pen_rgb(t->rgb[TC_FILLTEXT]);
    p->highlight = pen_rgb(t->rgb[TC_HIGHLIGHT]);
    p->side_bg = pen_rgb(t->rgb[TC_BARBLOCK]);
    p->side_sel = pen_rgb(t->rgb[TC_EXTRA1]);
    p->side_text = p->text;
    p->side_dim = p->dim;
    p->sep = p->side_sel;
    p->pill_bg = p->side_sel;
    p->avatar[0] = p->accent;
    p->avatar[1] = pen_rgb(t->rgb[TC_BARTRIM]);
    p->avatar[2] = pen_rgb(t->rgb[TC_EXTRA3]);
    p->avatar[3] = pen_rgb(t->rgb[TC_EXTRA4]);
    p->avatar[4] = pen_rgb(t->rgb[TC_EXTRA5]);
    p->avatar[5] = pen_rgb(t->rgb[TC_EXTRA6]);
    alloc_emoji_pens();
}

static void free_pens(void)
{
    int i;
    if (!G.scr)
        return;
    for (i = 0; i < G.n_obtained; i++)
        ReleasePen(G.scr->ViewPort.ColorMap, (ULONG)G.obtained[i]);
    G.n_obtained = 0;
}

static int open_main(void)
{
    Object *root;
    struct DrawInfo *dri;
    ULONG minside, hdr_h;

    if (open_screen() < 0)
        return -1;
    dri = GetScreenDrawInfo(G.scr);
    G.hipen = dri ? dri->dri_Pens[HIGHLIGHTTEXTPEN] : 2;
    G.fillpen = dri ? dri->dri_Pens[FILLPEN] : 3;
    if (dri)
        FreeScreenDrawInfo(G.scr, dri);
    /* room list wide enough for about 22 characters of the screen font */
    minside = (ULONG)(G.scr->RastPort.Font ? G.scr->RastPort.Font->tf_XSize : 8) * 24;
    hdr_h = (ULONG)(G.scr->RastPort.Font ? G.scr->RastPort.Font->tf_YSize : 8) * 2 + 2;
    alloc_pens();
    G.vi = GetVisualInfo(G.scr, TAG_END);
    build_menu();

    G.render_hook.h_Entry = (HOOKFUNC)HookEntry;
    G.render_hook.h_SubEntry = (HOOKFUNC)render_hook_func;
    G.idcmp_hook.h_Entry = (HOOKFUNC)HookEntry;
    G.idcmp_hook.h_SubEntry = (HOOKFUNC)idcmp_hook_func;
    G.edit_hook.h_Entry = (HOOKFUNC)HookEntry;
    G.edit_hook.h_SubEntry = (HOOKFUNC)edit_hook_func;

    root = G.g[0] = VLayoutObject,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_InnerSpacing, 0,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_InnerSpacing, 0,
            LAYOUT_AddChild, G.g[G_ROOMS] = SpaceObject,
                GA_ID, G_ROOMS,
                GA_RelVerify, TRUE,
                ICA_TARGET, ICTARGET_IDCMP,
                SPACE_Transparent, TRUE,
                SPACE_RenderHook, (ULONG)&G.render_hook,
            End,
            CHILD_WeightedWidth, 28,
            CHILD_MinWidth, minside,
            LAYOUT_AddChild, G.g[G_SIDESCROLL] = ScrollerObject,
                GA_ID, G_SIDESCROLL,
                GA_RelVerify, TRUE,
                SCROLLER_Orientation, SORIENT_VERT,
                SCROLLER_Arrows, TRUE,
                ICA_TARGET, ICTARGET_IDCMP,
            End,
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, VLayoutObject,
                LAYOUT_InnerSpacing, 0,
                LAYOUT_AddChild, G.g[G_ROOMHDR] = SpaceObject,
                    GA_ID, G_ROOMHDR,
                    SPACE_Transparent, TRUE,
                    SPACE_MinHeight, hdr_h,
                    SPACE_RenderHook, (ULONG)&G.render_hook,
                End,
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, HLayoutObject,
                    LAYOUT_InnerSpacing, 0,
                    LAYOUT_AddChild, G.g[G_MSGS] = SpaceObject,
                        GA_ID, G_MSGS,
                        GA_RelVerify, TRUE,
                        ICA_TARGET, ICTARGET_IDCMP,
                        SPACE_Transparent, TRUE,
                        SPACE_RenderHook, (ULONG)&G.render_hook,
                    End,
                    LAYOUT_AddChild, G.g[G_CHATSCROLL] = ScrollerObject,
                        GA_ID, G_CHATSCROLL,
                        GA_RelVerify, TRUE,
                        SCROLLER_Orientation, SORIENT_VERT,
                        SCROLLER_Arrows, TRUE,
                        ICA_TARGET, ICTARGET_IDCMP,
                    End,
                    CHILD_WeightedWidth, 0,
                End,
                LAYOUT_AddChild, HLayoutObject,
                    LAYOUT_SpaceOuter, TRUE,
                    LAYOUT_AddChild, G.g[G_INPUT] = StringObject,
                        GA_ID, G_INPUT,
                        GA_RelVerify, TRUE,
                        STRINGA_MaxChars, INPUT_MAX,
                        STRINGA_EditHook, (ULONG)&G.edit_hook,   /* every keystroke: suggestions */
                    End,
                    LAYOUT_AddChild, G.g[G_SEND] = ButtonObject,
                        GA_ID, G_SEND,
                        GA_RelVerify, TRUE,
                        GA_Text, (ULONG)"_Send",
                    End,
                    CHILD_WeightedWidth, 0,
                End,
                CHILD_WeightedHeight, 0,
            End,
            CHILD_WeightedWidth, 72,
        End,
        LAYOUT_AddChild, G.g[G_STATUS] = ButtonObject,
            GA_ID, G_STATUS,
            GA_ReadOnly, TRUE,
            GA_Text, (ULONG)G.status,
            BUTTON_Justification, BCJ_LEFT,
        End,
        CHILD_WeightedHeight, 0,
    End;
    if (!root)
        return -1;
    G.win_obj = WindowObject,
        WA_Title, (ULONG)G.title,
        WA_ScreenTitle, (ULONG)"AmiRocket " AR_VERSION,
        WA_PubScreen, (ULONG)G.scr,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_Activate, TRUE,
        WA_Width, G.prefs.win_width,
        WA_Height, G.prefs.win_height,
        G.prefs.win_left >= 0 ? WA_Left : TAG_IGNORE, G.prefs.win_left,
        G.prefs.win_top >= 0 ? WA_Top : TAG_IGNORE, G.prefs.win_top,
        G.prefs.win_left < 0 ? WINDOW_Position : TAG_IGNORE, WPOS_CENTERSCREEN,
        G.menu ? WINDOW_MenuStrip : TAG_IGNORE, (ULONG)G.menu,
        WA_IDCMP, IDCMP_GADGETDOWN | IDCMP_GADGETUP | IDCMP_CLOSEWINDOW | IDCMP_MENUPICK |
                  IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW | IDCMP_RAWKEY | IDCMP_VANILLAKEY |
                  IDCMP_IDCMPUPDATE | IDCMP_EXTENDEDMOUSE | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW,
        WINDOW_IDCMPHook, (ULONG)&G.idcmp_hook,
        WINDOW_IDCMPHookBits, IDCMP_IDCMPUPDATE | IDCMP_EXTENDEDMOUSE,
        WINDOW_ParentGroup, (ULONG)root,
    End;
    if (!G.win_obj) {
        DisposeObject(root);
        return -1;
    }
    G.win = RA_OpenWindow(G.win_obj);
    return G.win ? 0 : -1;
}

static void remember_window(void)
{
    if (!G.win)
        return;
    G.prefs.win_left = G.win->LeftEdge;
    G.prefs.win_top = G.win->TopEdge;
    G.prefs.win_width = G.win->Width;
    G.prefs.win_height = G.win->Height;
}

static void close_main(void)
{
    if (G.win_obj)
        DisposeObject(G.win_obj);
    G.win_obj = NULL;
    G.win = NULL;
    if (G.menu)
        FreeMenus(G.menu);
    G.menu = NULL;
    ar_free(G.newmenu);
    G.newmenu = NULL;
    if (G.vi)
        FreeVisualInfo(G.vi);
    G.vi = NULL;
    free_pens();
    close_screen();
}

/* Switches theme: closes the windows and screen and opens them again. */
static void apply_theme(int idx)
{
    int had_login = G.lwin_obj != NULL;

    ar_strlcpy(G.prefs.theme, theme_get(idx)->id, sizeof(G.prefs.theme));
    remember_window();
    prefs_save(&G.prefs);
    login_close();
    close_main();
    if (open_main() < 0) {
        /* last resort: back to Workbench */
        ar_strlcpy(G.prefs.theme, "workbench", sizeof(G.prefs.theme));
        if (open_main() < 0) {
            G.quit = 1;
            return;
        }
    }
    G.chat.width = 0;
    rooms_rebuild();
    msgs_rebuild(1);
    update_title();
    set_status(G.status);
    if (had_login)
        login_open(NULL);
}

int gui_run(void)
{
    char err[128];
    int rc = RETURN_OK;

    memset(&G, 0, sizeof(G));
    NewList(&G.room_list);
    NewList(&G.msg_list);
    rc_roomlist_init(&G.rooms);
    rc_msglist_init(&G.msgs);
    ar_strlcpy(G.title, "AmiRocket", sizeof(G.title));
    ar_strlcpy(G.status, "Not connected", sizeof(G.status));
    ar_strlcpy(G.roomhdr, "Not connected", sizeof(G.roomhdr));
    side_init(&G.side);
    chat_init(&G.chat);
    G.dirty_sig = AllocSignal(-1);

    if (open_libs() < 0) {
        close_libs();
        return RETURN_FAIL;
    }
    prefs_load(&G.prefs);
    update_tz(0);
    G.port = CreateMsgPort();
    if (!G.port || worker_start(G.port, err, sizeof(err)) < 0) {
        request("AmiRocket", G.port ? err : "Out of memory");
        rc = RETURN_FAIL;
        goto out;
    }
    if (G.prefs.interval != 5) {
        NetMsg *m = netmsg_new(NC_SET_INTERVAL);
        if (m) {
            m->a_interval = G.prefs.interval;
            send_cmd(m);
        }
    }
    if (open_main() < 0) {
        request("AmiRocket", "Cannot open the main window.");
        rc = RETURN_FAIL;
        goto out;
    }

    if (G.prefs.remember && G.prefs.token[0] && G.prefs.user_id[0] && G.prefs.server[0])
        cmd_connect_token();
    else
        login_open(NULL);

    while (!G.quit) {
        ULONG wsig = 0, lsig = 0, sigs;
        if (G.win_obj)
            GetAttr(WINDOW_SigMask, G.win_obj, &wsig);
        if (G.lwin_obj)
            GetAttr(WINDOW_SigMask, G.lwin_obj, &lsig);
        input_events();
        if (G.dirty)
            redraw_dirty();
        sigs = Wait(wsig | lsig | (1UL << G.port->mp_SigBit) | SIGBREAKF_CTRL_C |
                    (G.dirty_sig >= 0 ? 1UL << G.dirty_sig : 0));
        if (sigs & SIGBREAKF_CTRL_C)
            G.quit = 1;
        if (sigs & (1UL << G.port->mp_SigBit))
            handle_port();
        if (wsig && (sigs & wsig))
            handle_main();
        if (G.lwin_obj && (sigs & lsig))
            handle_login();
    }
    remember_window();
    prefs_save(&G.prefs);

out:
    worker_stop();
    login_close();
    close_main();
    free_list(&G.room_list);
    free_list(&G.msg_list);
    side_free(&G.side);
    chat_free(&G.chat);
    ar_free(G.members);
    if (G.dirty_sig >= 0)
        FreeSignal(G.dirty_sig);
    rc_roomlist_free(&G.rooms);
    rc_msglist_free(&G.msgs);
    if (G.port) {
        struct Message *msg;
        while ((msg = GetMsg(G.port)) != NULL)
            if (msg->mn_Node.ln_Type == NT_REPLYMSG)
                netmsg_free((NetMsg *)msg);
        DeleteMsgPort(G.port);
    }
    close_libs();
    return rc;
}

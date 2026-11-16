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
#include "worker.h"
#include "../core/charset.h"
#include "../core/mem.h"
#include "../core/timeutil.h"
#include "../core/url.h"
#include "../core/util.h"

#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <libraries/gadtools.h>
#include <graphics/gfxbase.h>
#include <libraries/locale.h>

#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/listbrowser.h>
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
#include <proto/checkbox.h>
#include <proto/label.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *GadToolsBase;
struct Library *UtilityBase;
struct LocaleBase *LocaleBase;
struct Library *WindowBase, *LayoutBase, *ButtonBase, *StringBase;
struct Library *ListBrowserBase, *CheckBoxBase, *LabelBase;

#define MAX_MESSAGES 300
#define INPUT_MAX    4000

enum {
    G_ROOMS = 1, G_MSGS, G_INPUT, G_SEND, G_STATUS,
    L_SERVER, L_USER, L_PASS, L_CODE, L_VERIFY, L_REMEMBER, L_CONNECT, L_CANCEL,
    G_COUNT
};

enum { M_CONNECT = 1, M_DISCONNECT, M_LOGOUT, M_ABOUT, M_QUIT, M_REFRESH };

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
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static struct ColumnInfo g_msg_cols[] = {
    { 9,  (STRPTR)"", 0 },
    { 16, (STRPTR)"", 0 },
    { 75, (STRPTR)"", 0 },
    { -1, NULL, 0 }
};

typedef struct {
    struct Screen *scr;
    APTR vi;
    struct Menu *menu;
    UWORD hipen;
    struct MsgPort *port;      /* worker replies and events */

    Object *win_obj, *g[G_COUNT];
    struct Window *win;
    Object *lwin_obj, *lroot;
    struct Window *lwin;

    struct List room_list, msg_list;

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

static void update_title(void)
{
    const RcRoom *r = G.cur_rid[0] ? find_room(G.cur_rid) : NULL;
    Buf b;

    buf_init(&b);
    buf_puts(&b, "AmiRocket");
    if (r) {
        buf_puts(&b, " - ");
        buf_putc(&b, r->type == 'd' ? '@' : '#');
        to_latin1(&b, r->type == 'd' ? r->name : rc_room_title(r), 0);
    }
    ar_strlcpy(G.title, buf_str(&b), sizeof(G.title));
    buf_free(&b);
    if (G.win)
        SetWindowTitles(G.win, (STRPTR)G.title, (STRPTR)~0);
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

/* ---- lists --------------------------------------------------------- */

static void free_list(struct List *l)
{
    struct Node *n;
    while ((n = RemHead(l)) != NULL)
        FreeListBrowserNode(n);
}

static void rooms_rebuild(void)
{
    int i, sel = -1;
    Buf b;

    if (G.win)
        SetGadgetAttrs((struct Gadget *)G.g[G_ROOMS], G.win, NULL, LISTBROWSER_Labels, ~0, TAG_END);
    free_list(&G.room_list);
    buf_init(&b);
    for (i = 0; i < G.rooms.count; i++) {
        const RcRoom *r = &G.rooms.items[i];
        struct Node *n;
        buf_clear(&b);
        buf_puts(&b, r->type == 'd' ? "@ " : r->type == 'p' ? "* " : "# ");
        to_latin1(&b, r->type == 'd' ? r->name : rc_room_title(r), 0);
        if (r->unread > 0)
            buf_printf(&b, " (%d)", r->unread);
        else if (r->alert)
            buf_puts(&b, " (!)");
        n = AllocListBrowserNode(1,
                                 LBNA_UserData, (ULONG)r->rid,
                                 LBNA_Column, 0,
                                 LBNCA_CopyText, TRUE,
                                 LBNCA_Text, (ULONG)buf_str(&b),
                                 (r->unread || r->alert) ? LBNCA_FGPen : TAG_IGNORE, (ULONG)G.hipen,
                                 TAG_END);
        if (n)
            AddTail(&G.room_list, n);
        if (!strcmp(r->rid, G.cur_rid))
            sel = i;
    }
    buf_free(&b);
    if (G.win)
        SetGadgetAttrs((struct Gadget *)G.g[G_ROOMS], G.win, NULL,
                       LISTBROWSER_Labels, (ULONG)&G.room_list,
                       LISTBROWSER_Selected, (ULONG)sel, TAG_END);
}

static int add_row(const char *time, const char *nick, const char *text, int highlight)
{
    struct Node *n = AllocListBrowserNode(3,
        LBNA_Column, 0,
            LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)time,
        LBNA_Column, 1,
            LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)nick,
            highlight & 2 ? LBNCA_FGPen : TAG_IGNORE, (ULONG)G.hipen,
        LBNA_Column, 2,
            LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)text,
            LBNCA_WordWrap, TRUE,
            highlight & 1 ? LBNCA_FGPen : TAG_IGNORE, (ULONG)G.hipen,
        TAG_END);
    if (!n)
        return 0;
    AddTail(&G.msg_list, n);
    return 1;
}

static int mentions_me(const char *utf8)
{
    char tag[100];
    const char *p;
    size_t n;

    if (!G.username[0] || !utf8)
        return 0;
    snprintf(tag, sizeof(tag), "@%s", G.username);
    n = strlen(tag);
    for (p = utf8; (p = strchr(p, '@')) != NULL; p++) {
        if (!ar_strncasecmp(p, tag, n) || !strncmp(p, "@all", 4) || !strncmp(p, "@here", 5)) {
            char c = p[ !ar_strncasecmp(p, tag, n) ? n : (p[1] == 'a' ? 4 : 5) ];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
                return 1;
        }
    }
    return 0;
}

static void msgs_rebuild(int scroll_bottom)
{
    long lastday = -1;
    int i, rows = 0;
    Buf text, nick;

    if (G.win)
        SetGadgetAttrs((struct Gadget *)G.g[G_MSGS], G.win, NULL, LISTBROWSER_Labels, ~0, TAG_END);
    free_list(&G.msg_list);
    buf_init(&text);
    buf_init(&nick);
    for (i = 0; i < G.msgs.count; i++) {
        const RcMsg *m = &G.msgs.items[i];
        ar_ms local = m->ts_ms + G.tz_ms;
        long day = ms_day_number(local);
        const char *sys = rc_system_text(m->type);
        char hm[8];
        int hl = 0;
        char *line, *nl;

        if (day != lastday) {
            char date[16], sep[40];
            ms_format_date(local, date);
            snprintf(sep, sizeof(sep), "------ %s ------", date);
            rows += add_row("", "", sep, 0);
            lastday = day;
        }
        ms_format_hm(local, hm);
        buf_clear(&text);
        buf_clear(&nick);
        if (sys) {
            buf_puts(&nick, "*");
            to_latin1(&text, m->username, 0);
            buf_putc(&text, ' ');
            buf_puts(&text, sys);
            if (m->text && *m->text && strcmp(m->type, "rm") != 0) {
                buf_putc(&text, ' ');
                to_latin1(&text, m->text, 0);
            }
        } else {
            to_latin1(&nick, m->username, 0);
            if (m->tmid)
                buf_puts(&text, "[thread] ");
            to_latin1(&text, m->text, 1);
            if (m->n_attachments) {
                if (m->text && *m->text)
                    buf_putc(&text, ' ');
                buf_puts(&text, "[file: ");
                to_latin1(&text, m->attachment ? m->attachment : "attachment", 0);
                buf_putc(&text, ']');
            }
            if (m->edited)
                buf_puts(&text, " (edited)");
            if (mentions_me(m->text))
                hl |= 1;
            if (G.username[0] && !strcmp(m->username, G.username))
                hl |= 2;
        }
        /* one listbrowser row per text line */
        line = text.data ? text.data : (char *)"";
        do {
            nl = strchr(line, '\n');
            if (nl)
                *nl = 0;
            rows += add_row(line == text.data ? hm : "", line == text.data ? buf_str(&nick) : "", line, hl);
            line = nl ? nl + 1 : NULL;
        } while (line);
    }
    buf_free(&text);
    buf_free(&nick);
    if (G.win) {
        SetGadgetAttrs((struct Gadget *)G.g[G_MSGS], G.win, NULL,
                       LISTBROWSER_Labels, (ULONG)&G.msg_list,
                       LISTBROWSER_Selected, (ULONG)-1, TAG_END);
        if (scroll_bottom && rows > 0)
            SetGadgetAttrs((struct Gadget *)G.g[G_MSGS], G.win, NULL,
                           LISTBROWSER_MakeVisible, (ULONG)(rows - 1), TAG_END);
    }
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
    update_tz(m->server_now);
    msgs_rebuild(1);
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
        msgs_rebuild(1);
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

static void on_room_clicked(void)
{
    struct Node *n = NULL;
    char *rid = NULL;

    GetAttr(LISTBROWSER_SelectedNode, G.g[G_ROOMS], (ULONG *)&n);
    if (!n)
        return;
    GetListBrowserNodeAttrs(n, LBNA_UserData, (ULONG)&rid, TAG_END);
    if (!rid || !strcmp(rid, G.cur_rid))
        return;
    ar_strlcpy(G.cur_rid, rid, sizeof(G.cur_rid));
    rc_msglist_free(&G.msgs);
    msgs_rebuild(0);
    update_title();
    cmd_open_room(G.cur_rid);
    ActivateLayoutGadget((struct Gadget *)G.g[0], G.win, NULL, (ULONG)G.g[G_INPUT]);
}

static void about(void)
{
    snprintf(G.about, sizeof(G.about),
             "AmiRocket " AR_VERSION "\n"
             "A Rocket.Chat client for AmigaOS 3.2\n\n"
             "Networking: bsdsocket.library, TLS: AmiSSL\n"
             "Messages are fetched every %d seconds.", G.prefs.interval);
    request("About AmiRocket", G.about);
}

static void handle_menu(UWORD code)
{
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
        }
        code = item->NextSelect;
    }
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
        case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
            case G_ROOMS:
                on_room_clicked();
                break;
            case G_INPUT:
                if (code != 9 && code != 0x5F)   /* not Tab / Help */
                    cmd_send_input();
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
        &LabelBase, &CheckBoxBase, &ListBrowserBase, &StringBase, &ButtonBase, &LayoutBase,
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

static int open_main(void)
{
    Object *root;
    struct DrawInfo *dri;

    G.scr = LockPubScreen(NULL);
    if (!G.scr)
        return -1;
    dri = GetScreenDrawInfo(G.scr);
    G.hipen = dri ? dri->dri_Pens[HIGHLIGHTTEXTPEN] : 2;
    if (dri)
        FreeScreenDrawInfo(G.scr, dri);
    G.vi = GetVisualInfo(G.scr, TAG_END);
    G.menu = CreateMenus(g_newmenu, GTMN_FrontPen, 0, TAG_END);
    if (G.menu && G.vi)
        LayoutMenus(G.menu, G.vi, GTMN_NewLookMenus, TRUE, TAG_END);

    root = G.g[0] = VLayoutObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, G.g[G_ROOMS] = ListBrowserObject,
                GA_ID, G_ROOMS,
                GA_RelVerify, TRUE,
                LISTBROWSER_Labels, (ULONG)&G.room_list,
                LISTBROWSER_ShowSelected, TRUE,
                LISTBROWSER_AutoWheel, TRUE,
            End,
            CHILD_WeightedWidth, 25,
            CHILD_MinWidth, 100,
            LAYOUT_AddChild, VLayoutObject,
                LAYOUT_AddChild, G.g[G_MSGS] = ListBrowserObject,
                    GA_ID, G_MSGS,
                    GA_ReadOnly, TRUE,
                    LISTBROWSER_Labels, (ULONG)&G.msg_list,
                    LISTBROWSER_ColumnInfo, (ULONG)g_msg_cols,
                    LISTBROWSER_WrapText, TRUE,
                    LISTBROWSER_AutoWheel, TRUE,
                    LISTBROWSER_VertSeparators, FALSE,
                End,
                LAYOUT_AddChild, HLayoutObject,
                    LAYOUT_AddChild, G.g[G_INPUT] = StringObject,
                        GA_ID, G_INPUT,
                        GA_RelVerify, TRUE,
                        STRINGA_MaxChars, INPUT_MAX,
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
            CHILD_WeightedWidth, 75,
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
        GetAttr(WINDOW_SigMask, G.win_obj, &wsig);
        if (G.lwin_obj)
            GetAttr(WINDOW_SigMask, G.lwin_obj, &lsig);
        sigs = Wait(wsig | lsig | (1UL << G.port->mp_SigBit) | SIGBREAKF_CTRL_C);
        if (sigs & SIGBREAKF_CTRL_C)
            G.quit = 1;
        if (sigs & (1UL << G.port->mp_SigBit))
            handle_port();
        if (sigs & wsig)
            handle_main();
        if (G.lwin_obj && (sigs & lsig))
            handle_login();
    }
    remember_window();
    prefs_save(&G.prefs);

out:
    worker_stop();
    login_close();
    if (G.win_obj)
        DisposeObject(G.win_obj);
    free_list(&G.room_list);
    free_list(&G.msg_list);
    if (G.menu)
        FreeMenus(G.menu);
    if (G.vi)
        FreeVisualInfo(G.vi);
    if (G.scr)
        UnlockPubScreen(NULL, G.scr);
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

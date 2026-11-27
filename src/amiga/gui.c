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
#include "prefs_ipc.h"
#include "plugins.h"
#include "arexx.h"
#include "theme.h"
#include "view.h"
#include "emoji_data.h"
#include "../core/emoji.h"
#include "worker.h"
#include "../core/charset.h"
#include "../core/drafts.h"
#include <devices/timer.h>
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
#include <intuition/classes.h>
#include <intuition/sghooks.h>
#include <devices/inputevent.h>
#include <devices/clipboard.h>
#include <libraries/gadtools.h>
#include <utility/hooks.h>
#include <graphics/regions.h>
#include <graphics/gfxbase.h>
#include <graphics/gfx.h>
#include <libraries/locale.h>

#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/fuelgauge.h>
#include <datatypes/soundclass.h>
#include <proto/fuelgauge.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/texteditor.h>
#include <libraries/asl.h>
#include <graphics/text.h>
#include <datatypes/pictureclass.h>
#include <datatypes/datatypesclass.h>
#include <proto/datatypes.h>
#include <proto/asl.h>
#include <proto/diskfont.h>
#include <proto/texteditor.h>
#include <dos/dostags.h>
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
#include <proto/icon.h>
#include <workbench/workbench.h>
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
struct Library *FuelGaugeBase;
struct Library *WindowBase, *LayoutBase, *ButtonBase, *StringBase;
struct Library *IconBase;
struct Library *AslBase, *DiskfontBase, *TextFieldBase, *DataTypesBase;
struct Library *ListBrowserBase, *CheckBoxBase, *LabelBase, *SpaceBase, *ScrollerBase;

#define MAX_MESSAGES memory_message_limit()
#define INPUT_MAX    4000
#define MAX_SUGG     8

enum {
    G_ROOMS = 1, G_MSGS, G_INPUT, G_SEND, G_STATUS, G_ROOMHDR, G_SIDESCROLL, G_CHATSCROLL, G_FILTER, G_COMPOSELABEL,
    L_SERVER, L_USER, L_PASS, L_CODE, L_VERIFY, L_REMEMBER, L_CONNECT, L_CANCEL,
    G_NEW, G_PREFSB, G_EMOJI, G_BOLD, G_ITALIC, G_STRIKE, G_INLINECODE, G_CODEBLOCK, G_LINK, G_CONNECTB, G_UPLOAD, G_SEARCH, G_FIRSTUNREAD, G_RETRYSEND,
    G_COUNT
};

enum { M_CONNECT = 1, M_DISCONNECT, M_LOGOUT, M_ABOUT, M_QUIT, M_REFRESH, M_OWNSCREEN, M_FONT, M_SCREENFONT, M_PREVIEW, M_OLDER, M_SEND, M_COMPLETE, M_NEXTSUGG, M_PREVSUGG, M_COPY, M_CUT, M_PASTE, M_COPYMSG, M_PREFS, M_NEWDM, M_NEWCHANNEL, M_NEWPRIVATE, M_SAVEFILE, M_CLEARIMAGES, M_ICONIFY, M_NEXTUNREAD, M_JUMPUNREAD, M_SEARCH, M_UPLOAD, M_CLEARCACHE, M_MESSAGE, M_OPENIMAGE, M_CLEAREXTERNAL, M_RETRYSEND, M_SWITCHNEXT, M_SWITCHPREV, M_QUICKROOM, M_FORGETSEND, M_THEME_BASE = 100, M_PLUGIN_BASE = 200 };

static struct NewMenu g_newmenu[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Connect...", NULL, 0, 0, (APTR)M_CONNECT },
    { NM_ITEM, (STRPTR)"Disconnect", (STRPTR)"D", 0, 0, (APTR)M_DISCONNECT },
    { NM_ITEM, (STRPTR)"Log out and forget login", NULL, 0, 0, (APTR)M_LOGOUT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Iconify", (STRPTR)"H", 0, 0, (APTR)M_ICONIFY },
    { NM_ITEM, (STRPTR)"Preferences...", (STRPTR)",", 0, 0, (APTR)M_PREFS },
    { NM_ITEM, (STRPTR)"About...", (STRPTR)"?", 0, 0, (APTR)M_ABOUT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)M_QUIT },
    { NM_TITLE, (STRPTR)"Room", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"New direct / group message...", (STRPTR)"N", 0, 0, (APTR)M_NEWDM },
    { NM_ITEM, (STRPTR)"New public channel...", NULL, 0, 0, (APTR)M_NEWCHANNEL },
    { NM_ITEM, (STRPTR)"New private channel...", NULL, 0, 0, (APTR)M_NEWPRIVATE },
    { NM_ITEM, (STRPTR)"Search this conversation...", (STRPTR)"F", 0, 0, (APTR)M_SEARCH },
    { NM_ITEM, (STRPTR)"Upload attachment...", NULL, 0, 0, (APTR)M_UPLOAD },
    { NM_ITEM, (STRPTR)"Clear media caches", NULL, 0, 0, (APTR)M_CLEARCACHE },
    { NM_ITEM, (STRPTR)"Clear image previews", NULL, 0, 0, (APTR)M_CLEARIMAGES },
    { NM_ITEM, (STRPTR)"Open selected image externally", NULL, 0, 0, (APTR)M_OPENIMAGE },
    { NM_ITEM, (STRPTR)"Clear temporary image files...", NULL, 0, 0, (APTR)M_CLEAREXTERNAL },
    { NM_ITEM, (STRPTR)"Save selected attachment...", NULL, 0, 0, (APTR)M_SAVEFILE },
    { NM_ITEM, (STRPTR)"Next unread room", (STRPTR)"U", 0, 0, (APTR)M_NEXTUNREAD },
    { NM_ITEM, (STRPTR)"Jump to new messages", (STRPTR)"L", 0, 0, (APTR)M_JUMPUNREAD },
    { NM_ITEM, (STRPTR)"Check for new messages", (STRPTR)"R", 0, 0, (APTR)M_REFRESH },
    { NM_ITEM, (STRPTR)"Load older messages", (STRPTR)"O", 0, 0, (APTR)M_OLDER },
    { NM_ITEM, (STRPTR)"Retry failed send...", NULL, 0, 0, (APTR)M_RETRYSEND },
    { NM_ITEM, (STRPTR)"Forget unconfirmed delivery...", NULL, 0, 0, (APTR)M_FORGETSEND },
    { NM_ITEM, (STRPTR)"Next conversation", NULL, 0, 0, (APTR)M_SWITCHNEXT },
    { NM_ITEM, (STRPTR)"Previous conversation", NULL, 0, 0, (APTR)M_SWITCHPREV },
    { NM_ITEM, (STRPTR)"Find conversation (Ctrl+K)", NULL, 0, 0, (APTR)M_QUICKROOM },
    { NM_ITEM, (STRPTR)"Send message", (STRPTR)"S", 0, 0, (APTR)M_SEND },
    { NM_ITEM, (STRPTR)"Insert suggested name / emoji", (STRPTR)"I", 0, 0, (APTR)M_COMPLETE },
    { NM_ITEM, (STRPTR)"Next suggestion", (STRPTR)"J", 0, 0, (APTR)M_NEXTSUGG },
    { NM_ITEM, (STRPTR)"Previous suggestion", (STRPTR)"K", 0, 0, (APTR)M_PREVSUGG },
    { NM_TITLE, (STRPTR)"Edit", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Copy", (STRPTR)"C", 0, 0, (APTR)M_COPY },
    { NM_ITEM, (STRPTR)"Cut", (STRPTR)"X", 0, 0, (APTR)M_CUT },
    { NM_ITEM, (STRPTR)"Paste", (STRPTR)"V", 0, 0, (APTR)M_PASTE },
    { NM_ITEM, (STRPTR)"Message / thread actions...", (STRPTR)"T", 0, 0, (APTR)M_MESSAGE },
    { NM_ITEM, (STRPTR)"Copy selected message", (STRPTR)"B", 0, 0, (APTR)M_COPYMSG },
    { NM_END, NULL, NULL, 0, 0, NULL }
};
#define BASE_MENU_ITEMS ((int)(sizeof(g_newmenu) / sizeof(g_newmenu[0])) - 1)

enum { SEC_FAV, SEC_CHAN, SEC_DM, SEC_ROOMS, SEC_N };
static const char *g_sec_names[SEC_N] = { "Favorites", "Non favorites", "Direct messages", "Rooms" };

enum { DIRTY_SIDE = 1, DIRTY_CHAT = 2, DIRTY_HEADER = 4 };

typedef struct {
    RcRoomList rooms; RcMsgList msgs;
    char *members,*presence,*draft;
    char cur_rid[64],loading_rid[64],topic[256],username[96],host[256],anchor_id[64],presence_user_id[64],status[256];
    char unread_anchor[96],marker_room[64]; ar_ms read_boundary; int boundary_unread;
    int connected,busy,history_pending,history_exhausted,send_pending,room_serial,download_pending;
    ar_ms tz_ms; int tz_known,anchor_line,chat_top,chat_follow,cursor;
    char dock_snapshot[256];
    RcMsgList events; int event_serial,last_error; char connection_text[256];
    ar_ms retry_until,status_until; int cooling,health_retry;
    int generation,collapsed[SEC_N],expanded,reconnecting;
} ServerState;

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
    struct Hook render_hook, idcmp_hook, filter_hook;
    struct ChangeListener editor_listener;
    struct IClass *editor_class;
    volatile int editor_action;
    struct TextFont *chat_font, *code_font;
    char filter[96], compose_label[200];ar_ms status_until;
    struct { char user[96]; Buf bytes; Object *picture; int attempted, failed, size; struct BitMap *bitmap; char path[96]; } avatars[64];
    int avatar_count, avatar_pending, avatar_generation;
    struct { char *url; Buf bytes; Object *picture; struct BitMap *bitmap; int width, height, expanded, loading, failed; unsigned long touched; char path[512]; } images[8];
    int image_generation; unsigned long image_clock;
    int prefs_only, running_client, session_override, plugins_dirty, panel_depth, defer_display, last_unread;
    PrefsEndpoint *prefs_service;
    char *presence;
    char presence_user_id[64];
    STRPTR reopen_draft;
    int composer_overlong;
    char anchor_id[64];
    int anchor_line, history_pending, history_exhausted, send_pending, room_serial, unread_seek;
    char unread_anchor[96],marker_room[64]; ar_ms read_boundary; int boundary_unread;
    int download_pending;
    Object *notification_sound;char notification_sound_path[256];ar_ms notification_sound_at;
    Object *transfer_text,*transfer_gauge; struct Window *transfer_win;
    ar_ms transfer_started;
    ULONG transfer_serial; int transfer_dialog,transfer_rc,transfer_cancelled,transfer_save_offer; char transfer_status[256],transfer_progress_text[256];
    volatile int filter_changed;
    volatile int input_changed;   /* set by the string edit hook */
    char     hook_buf[INPUT_MAX + 1];  /* input text as of the last key */
    volatile int hook_pos;        /* cursor position as of the last key */
    volatile int sugg_move;       /* arrow keys while the panel is open */
    volatile int sugg_close;      /* Esc */
    int      sugg_sel;
    volatile int last_key;        /* last raw key the edit hook saw (shown in the panel) */
    BYTE     dirty_sig;
    ULONG    dirty;            /* DIRTY_* bits */
    int      chat_relayout;
    char     topic[256];       /* UTF-8 */
    char    *members;          /* open room: "username\tname\tstatus\n" lines (UTF-8) */
    struct { char user[96]; char name[96]; int online; int recent; int emoji; char ins[100]; } sugg[MAX_SUGG];
    int      sugg_kind;        /* 0 people (@), 1 emoji (:) */
    int      n_sugg;
    int      sugg_at, sugg_end;  /* '@' position and cursor in the input text */

    struct MsgPort *app_port; struct DiskObject *app_icon;
    int iconified,menu_rebuild;
    int multi,active_server,server_view;
    int settings_override,server_overrides;
    int focus_slot,focus_visible; char focus_rid[64];
    int side_menu_pending,side_menu_slot,side_menu_x,side_menu_y;
    ServerState servers[PREFS_SERVERS_MAX];
    Prefs prefs;
    RcRoomList rooms;
    RcMsgList msgs;
    char cur_rid[64];
    char copy_id[96], copy_rid[64];
    int copy_chat, copy_file;
    struct { char *url; char path[512],drawer[384]; int slot,generation; } external_images[4];
    ULONG external_serial; int external_pending;
    int action_active,action_busy,action_slot,action_generation,action_serial,action_offset,action_total,action_menu_pending;
    char action_room[64],action_id[96],action_thread[96],action_user[96],action_me[96],action_caption[128];
    Object *action_list,*action_preview,*action_editor,*action_info,*action_edit,*action_delete;
    struct Window *action_win; struct List action_nodes; RcMsgList action_results;
    char loading_rid[64];
    char username[96];
    char host[256];
    int connected, busy, quit;
    int inline_reaction_pending;
    int pending;               /* commands awaiting replies */
    ar_ms tz_ms;               /* local time minus UTC */
    int tz_known;

    char status[256];
    char title[160];
    char roomhdr[200];
    int  collapsed[SEC_N];
    char about[512];
    ArDrafts *drafts; char draft_key[512],draft_last[8001]; int draft_cursor,draft_warned;
    struct MsgPort *tick_port; struct timerequest *tick; int tick_open,tick_pending;
    int search_back[64],search_back_count,search_step;
    int search_active,search_busy,search_slot,search_generation,search_serial,search_offset,search_total,search_selected;
    char search_room[64],search_query[513],search_caption[128]; RcMsgList search_results; struct List search_nodes;
    Object *search_list,*search_preview,*search_info; struct Window *search_win;

} Gui;

static Gui G;
static int memory_message_limit(void) { int n=G.prefs.message_limit; if(n<50 || n>1000) n=1000; return G.prefs.low_memory && n>200?200:n; }
static size_t memory_image_limit(void) { int n=G.prefs.image_cache_kib; if(n<256 || n>8192) n=8192; if(G.prefs.low_memory && n>512) n=512; return (size_t)n*1024; }
static int draft_checkpoint(void);
static void send_state_refresh(void);
static void send_record_done(int,const NetMsg *);
static int draft_bind(const char *rid);
static void draft_start(void);
static void draft_sent(int slot,const char *rid,const char *text);
static void draft_stop(void);
static ULONG client_tick_signal(void);
static void client_tick_handle(void);
static void on_search(NetMsg *m);
static void search_window(void);
static void upload_attachment(void);
static void clear_media_caches(void);


static void server_legacy_load(int slot)
{
    ServerPrefs *p=&G.prefs.servers[slot];
    ar_strlcpy(G.prefs.server,p->url,sizeof(G.prefs.server)); ar_strlcpy(G.prefs.user,p->user,sizeof(G.prefs.user));
    ar_strlcpy(G.prefs.user_id,p->user_id,sizeof(G.prefs.user_id)); ar_strlcpy(G.prefs.token,p->token,sizeof(G.prefs.token));
    G.prefs.verify=p->verify; G.prefs.remember=p->remember;
}
static void server_legacy_save(void)
{
    ServerPrefs *p;
    if(!G.multi) return;
    p=&G.prefs.servers[G.active_server];
    ar_strlcpy(p->url,G.prefs.server,sizeof(p->url)); ar_strlcpy(p->user,G.prefs.user,sizeof(p->user));
    ar_strlcpy(p->user_id,G.prefs.user_id,sizeof(p->user_id)); ar_strlcpy(p->token,G.prefs.token,sizeof(p->token));
    p->verify=G.prefs.verify; p->remember=G.prefs.remember; G.prefs.servers_defined=1;
}
static const char *server_display(int slot);
static const char *server_health(int slot,char *out,int size);
static void server_event(int slot,int error,const char *text);
static void server_notice(int slot,const char *text);
static void server_context_popup(void);
static void sidebar_pointer(struct IntuiMessage *im);
static void server_select(int slot);
static void server_reconfigure(const Prefs *next);
static int server_connect(int slot,int interactive);
static void notify_dock_state(void);
static void dock_room_delta(int slot,const RcRoomList *delta);
static void tree_rebuild(void);
static int server_background(NetMsg *m);

static ar_ms amiga_local_ms(void);
static void mark_dirty(ULONG what);
static void send_cmd(NetMsg *m);

static void avatar_drop_pictures(void)
{
    int i;
    for (i = 0; i < G.avatar_count; i++) {
        if (G.avatars[i].picture) DisposeDTObject(G.avatars[i].picture);
        if (G.avatars[i].path[0]) { DeleteFile(G.avatars[i].path); G.avatars[i].path[0] = 0; }
        G.avatars[i].picture = NULL; G.avatars[i].bitmap = NULL;
        G.avatars[i].failed = 0;
    }
}

static void avatar_reset(void)
{
    int i;
    avatar_drop_pictures();
    for (i = 0; i < G.avatar_count; i++) buf_free(&G.avatars[i].bytes);
    memset(G.avatars, 0, sizeof(G.avatars));
    G.avatar_count = G.avatar_pending = 0; G.avatar_generation++;
    ar_free(G.presence); G.presence = NULL;
}

static int picture_dimensions_ok(const unsigned char *p, size_t n, unsigned long maxside, unsigned long maxpixels)
{
    unsigned long w = 0, h = 0;
    if (n >= 24 && !memcmp(p, "\211PNG\r\n\032\n", 8)) {
        w = ((unsigned long)p[16]<<24) | ((unsigned long)p[17]<<16) | ((unsigned long)p[18]<<8) | p[19];
        h = ((unsigned long)p[20]<<24) | ((unsigned long)p[21]<<16) | ((unsigned long)p[22]<<8) | p[23];
    } else if (n >= 10 && !memcmp(p,"GIF8",4)) {
        w = p[6] | ((unsigned long)p[7]<<8); h = p[8] | ((unsigned long)p[9]<<8);
    } else if (n > 4 && p[0] == 255 && p[1] == 216) {
        size_t i = 2;
        while (i + 4 < n) {
            unsigned marker, len;
            if (p[i++] != 255) return 0;
            while (i < n && p[i] == 255) i++;
            if (i >= n) return 0;
            marker = p[i++];
            if (marker == 217 || marker == 218) break;
            if (marker == 1 || (marker >= 208 && marker <= 215)) continue;
            if (i + 2 > n) break;
            len = (p[i]<<8) | p[i+1];
            if (len < 2 || len > n-i) return 0;
            if (marker >= 192 && marker <= 207 && marker != 196 && marker != 200 && marker != 204 && len >= 7) {
                h = (p[i+3]<<8) | p[i+4]; w = (p[i+5]<<8) | p[i+6]; break;
            }
            i += len;
        }
    }
    return w > 0 && h > 0 && w <= maxside && h <= maxside && w * h <= maxpixels;
}

static struct BitMap *avatar_get(const char *user, int size)
{
    int i;
    if (G.prefs.low_memory || !DataTypesBase || !user || !*user || strlen(user) >= 96) return NULL;
    for (i = 0; i < G.avatar_count; i++) if (!strcmp(G.avatars[i].user, user) && G.avatars[i].size == size) break;
    if (i == G.avatar_count) {
        if (i == 64) return NULL;
        ar_strlcpy(G.avatars[i].user, user, sizeof(G.avatars[i].user));
        int j;
        buf_init(&G.avatars[i].bytes); G.avatars[i].size = size;
        for (j = 0; j < G.avatar_count; j++) if (!strcmp(G.avatars[j].user, user)) {
            G.avatars[i].attempted = G.avatars[j].attempted;
            if (G.avatars[j].bytes.len) {
                size_t total = 0; int k;
                for (k = 0; k < G.avatar_count; k++) total += G.avatars[k].bytes.len;
                if (total + G.avatars[j].bytes.len <= 2UL * 1024UL * 1024UL)
                    buf_append(&G.avatars[i].bytes, G.avatars[j].bytes.data, G.avatars[j].bytes.len);
            }
            break;
        }
        G.avatar_count++;
    }
    if (!G.avatars[i].attempted && G.connected && !G.avatar_pending) {
        NetMsg *m = netmsg_new(NC_AVATAR);
        if (m) {
            m->a_user = ar_strdup(user); m->a_interval = G.avatar_generation;
            if (m->a_user) { G.avatars[i].attempted = 1; G.avatar_pending = 1; send_cmd(m); }
            else netmsg_free(m);
        }
    }
    if (G.avatars[i].size != size) {
        if (G.avatars[i].picture) DisposeDTObject(G.avatars[i].picture);
        if (G.avatars[i].path[0]) { DeleteFile(G.avatars[i].path); G.avatars[i].path[0] = 0; }
        G.avatars[i].picture = NULL; G.avatars[i].bitmap = NULL;
        G.avatars[i].failed = 0; G.avatars[i].size = size;
    }
    if (G.avatars[i].bytes.len && !picture_dimensions_ok((unsigned char *)G.avatars[i].bytes.data, G.avatars[i].bytes.len, 1024, 1024UL * 1024UL))
        G.avatars[i].failed = 1;
    if (G.avatars[i].bytes.len && !G.avatars[i].picture && !G.avatars[i].failed) {
        Object *picture = NewDTObject(NULL,
            DTA_SourceType, DTST_MEMORY, DTA_SourceAddress, (ULONG)G.avatars[i].bytes.data,
            DTA_SourceSize, (ULONG)G.avatars[i].bytes.len, DTA_GroupID, GID_PICTURE,
            PDTA_DestMode, PMODE_V43, PDTA_FreeSourceBitMap, TRUE, PDTA_Remap, TRUE, PDTA_Screen, (ULONG)G.scr, TAG_END);
        /* OS 3.2 picture datatypes may not implement DTST_MEMORY. */
        if (!picture) {
            char path[96]; BPTR f, lock;
            snprintf(path, sizeof(path), "T:AmiRocket-avatar-%08lx-%d.image", (unsigned long)FindTask(NULL), i);
            lock = Lock(path, ACCESS_READ);
            if (lock) UnLock(lock);
            else if ((f = Open(path, MODE_NEWFILE)) != 0) {
                LONG wrote = Write(f, G.avatars[i].bytes.data, (LONG)G.avatars[i].bytes.len);
                LONG closed = Close(f);
                ar_strlcpy(G.avatars[i].path, path, sizeof(G.avatars[i].path));
                if (wrote == (LONG)G.avatars[i].bytes.len && closed)
                    picture = NewDTObject((APTR)path, DTA_SourceType, DTST_FILE, DTA_GroupID, GID_PICTURE,
                        PDTA_DestMode, PMODE_V43, PDTA_FreeSourceBitMap, TRUE,
                        PDTA_Remap, TRUE, PDTA_Screen, (ULONG)G.scr, TAG_END);
            }
        }
        if (picture) {
            struct BitMapHeader *header = NULL;
            GetDTAttrs(picture, PDTA_BitMapHeader, (ULONG)&header, TAG_END);
            if (header && header->bmh_Width <= 1024 && header->bmh_Height <= 1024 &&
                DoMethod(picture, PDTM_SCALE, size, size, 0) &&
                DoDTMethod(picture, NULL, NULL, DTM_PROCLAYOUT, NULL, TRUE)) {
                GetDTAttrs(picture, PDTA_DestBitMap, (ULONG)&G.avatars[i].bitmap, TAG_END);
                if (G.avatars[i].bitmap && (GetBitMapAttr(G.avatars[i].bitmap, BMA_WIDTH) < (ULONG)size ||
                    GetBitMapAttr(G.avatars[i].bitmap, BMA_HEIGHT) < (ULONG)size)) G.avatars[i].bitmap = NULL;
            }
            if (G.avatars[i].bitmap) G.avatars[i].picture = picture;
            else DisposeDTObject(picture);
        }
        if (!G.avatars[i].picture) {
            G.avatars[i].failed = 1;
            if (G.avatars[i].path[0]) { DeleteFile(G.avatars[i].path); G.avatars[i].path[0] = 0; }
        }
    }
    return G.avatars[i].bitmap;
}

static int user_presence(const char *user)
{
    const char *p = G.presence;
    size_t n = strlen(user);
    while (p && *p) {
        const char *end = strchr(p, '\n'), *tab;
        if (!end) end = p + strlen(p);
        tab = memchr(p, '\t', (size_t)(end - p));
        if (tab && (size_t)(tab-p) == n && !ar_strncasecmp(p,user,n)) {
            size_t len = (size_t)(end-tab-1); const char *status = tab + 1;
            if (len == 6 && !strncmp(status,"online",6)) return 1;
            if (len == 4 && !strncmp(status,"away",4)) return 2;
            if (len == 4 && !strncmp(status,"busy",4)) return 3;
            return 0;
        }
        p = *end ? end + 1 : end;
    }
    return 0;
}

static void on_avatar(NetMsg *m)
{
    int i;
    if (m->a_interval != G.avatar_generation) return;
    G.avatar_pending = 0;
    { size_t bytes = 0;
      for (i = 0; i < G.avatar_count; i++) bytes += G.avatars[i].bytes.len;
      for (i = 0; i < G.avatar_count; i++) if (m->a_user && !strcmp(G.avatars[i].user, m->a_user)) {
        G.avatars[i].attempted = 1;
        if (m->rc == RC_OK && !G.avatars[i].bytes.len && bytes + m->avatar.len <= 2UL * 1024UL * 1024UL) {
            if (buf_append(&G.avatars[i].bytes, m->avatar.data, m->avatar.len) == 0) bytes += m->avatar.len;
        }
      }
    }
    mark_dirty(DIRTY_SIDE | DIRTY_CHAT);
}

static void on_presence(NetMsg *m)
{
    if (strcmp(m->user_id, G.presence_user_id)) return;
    ar_free(G.presence); G.presence = NULL;
    if (m->rc == RC_OK) { G.presence = m->members; m->members = NULL; }
    mark_dirty(DIRTY_SIDE);
}

/* ---- helpers ------------------------------------------------------- */

static void to_latin1(Buf *b, const char *utf8, int multiline)
{
    if (utf8)
        utf8_to_latin1(b, utf8, multiline);
}

static int prefs_save_client_state(void)
{
    Prefs *p = ar_malloc(sizeof(*p)); int rc;
    if (!p) return -1;
    server_legacy_save();
    if(prefs_lock()<0) { ar_free(p); return -1; }
    if(prefs_load(p)==-2) { prefs_unlock(); ar_free(p); return -1; }
    ar_strlcpy(p->server,G.prefs.server,sizeof(p->server)); ar_strlcpy(p->user,G.prefs.user,sizeof(p->user));
    ar_strlcpy(p->user_id,G.prefs.user_id,sizeof(p->user_id)); ar_strlcpy(p->token,G.prefs.token,sizeof(p->token));
    p->remember = G.prefs.remember; p->verify = G.prefs.verify;
    if(G.multi) {
        int i; prefs_migrate_servers(p);
        for(i=0;i<PREFS_SERVERS_MAX;i++) {
            const ServerPrefs *live=&G.prefs.servers[i]; ServerPrefs *saved=&p->servers[i];
            if(!G.session_override && !saved->url[0] && live->url[0]) *saved=*live;
            if(saved->url[0] && !strcmp(saved->url,live->url)) {
                ar_strlcpy(saved->user,live->user,sizeof(saved->user));
                ar_strlcpy(saved->user_id,live->user_id,sizeof(saved->user_id)); ar_strlcpy(saved->token,live->token,sizeof(saved->token));
                saved->remember=live->remember; saved->verify=live->verify;
            }
        }
    }

    { int i,j,mask; char key[64],value[64];
      snprintf(value,sizeof(value),"%d",G.active_server); prefs_set(p,"workspace.server",value);
      snprintf(value,sizeof(value),"%d",G.server_view); prefs_set(p,"workspace.events",value);
      for(i=0;i<PREFS_SERVERS_MAX;i++) {
          if(!G.prefs.servers[i].url[0] || strcmp(G.prefs.servers[i].url,p->servers[i].url)) continue;
          mask=G.servers[i].expanded; for(j=0;j<SEC_N;j++) mask|=G.servers[i].collapsed[j]<<(j+1);
          snprintf(key,sizeof(key),"workspace.%d.tree",i); snprintf(value,sizeof(value),"%d",mask); prefs_set(p,key,value);
          snprintf(key,sizeof(key),"workspace.%d.room",i); prefs_set(p,key,i==G.active_server?G.cur_rid:G.servers[i].cur_rid);
      }
      prefs_set(p,"workspace.sidebar",prefs_extra(&G.prefs,"workspace.sidebar","30"));
      prefs_set(p,"workspace.composer_rows",prefs_extra(&G.prefs,"workspace.composer_rows","4"));
    }
    p->win_left = G.prefs.win_left; p->win_top = G.prefs.win_top; p->win_width = G.prefs.win_width; p->win_height = G.prefs.win_height;
    prefs_service_publish(G.prefs_service,&G.prefs);
    rc = prefs_save(p); prefs_unlock(); memset(p,0,sizeof(*p)); ar_free(p); return rc;
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
    ar_strlcpy(G.status, latin1, sizeof(G.status));G.status_until=amiga_local_ms()+10000;
    mark_dirty(DIRTY_HEADER);
    if (G.win && G.g[G_STATUS])
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
            const char *q = r->name;
            int shown = 0;
            while (*q) {
                const char *end = strchr(q, ',');
                size_t n = end ? (size_t)(end - q) : strlen(q);
                if (!(strlen(G.username) == n && !ar_strncasecmp(q, G.username, n))) {
                    Buf part; buf_init(&part); buf_append(&part, q, n);
                    if (shown++) buf_puts(b, ", ");
                    to_latin1(b, buf_str(&part), 0); buf_free(&part);
                }
                if (!end) break;
                q = end + 1;
            }
            if (!shown) to_latin1(b, r->name, 0);
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
    send_state_refresh();
    const RcRoom *r = !G.server_view && G.cur_rid[0] ? find_room(G.cur_rid) : NULL;
    Buf b;

    buf_init(&b);
    buf_puts(&b, "AmiRocket");
    if(G.multi && G.prefs.server[0]) { buf_puts(&b," - "); buf_puts(&b,server_display(G.active_server)); }
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

    if (G.win)
        SetWindowTitles(G.win, (STRPTR)G.title, (STRPTR)~0);
    mark_dirty(DIRTY_HEADER);
    buf_clear(&b);
    buf_puts(&b, "Message ");
    if (r) room_label(&b, r, 0); else buf_puts(&b, "(select a room)");
    ar_strlcpy(G.compose_label, buf_str(&b), sizeof(G.compose_label));
    mark_dirty(DIRTY_HEADER);
    buf_free(&b);
    if(G.win && G.g[G_INPUT] && G.g[G_SEND]) {
        SetGadgetAttrs((struct Gadget *)G.g[G_INPUT],G.win,NULL,GA_Disabled,G.multi && G.server_view,TAG_END);
        SetGadgetAttrs((struct Gadget *)G.g[G_SEND],G.win,NULL,GA_Disabled,G.multi && G.server_view,TAG_END);
    }

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

static int conversation_visible(void);
static void sync_conversation_focus(void);
static int main_iconify(void);
static int main_restore(void);
static void remember_window(void);
static void cmd_open_room(const char *rid);
static void unread_resolve(void);

/* ---- views --------------------------------------------------------- */

static void free_list(struct List *l)
{
    struct Node *n;
    while ((n = RemHead(l)) != NULL)
        FreeListBrowserNode(n);
}

static int room_section(const RcRoom *r)
{
    if(r->type=='d') return SEC_DM;
    return r->favorite ? SEC_FAV : SEC_CHAN;
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
    struct TextFont *old_font;
    if (!area_box(G.g[G_ROOMS], &box))
        return;
    old_font = G.win->RPort->Font;
    if (G.chat_font) SetFont(G.win->RPort, G.chat_font);
    { int i, last;
      G.side.visible = box.Height / (G.win->RPort->TxHeight + 5);
      side_scroll_to(&G.side, G.side.top);
      last = G.side.top + G.side.visible;
      for (i = G.side.top; i < G.side.n && i < last; i++) {
          SRow *row = &G.side.r[i];
          row->bitmap = NULL;
          if (row->is_dm && !row->participants && (!G.multi || row->server_slot==G.active_server)) {
              row->presence = (UBYTE)user_presence(row->avatar_user);
              row->bitmap = G.prefs.show_avatars ? avatar_get(row->avatar_user, G.win->RPort->TxHeight + 4) : NULL;
          }
      }
    }
    G.side.no_avatars = !G.prefs.show_avatars || G.prefs.low_memory;
    side_draw(&G.side, G.win->RPort, &box, &G.pens);
    SetFont(G.win->RPort, old_font);
    set_scroller(G_SIDESCROLL, G.side.top, G.side.visible, G.side.n);
    { struct IntuiMessage point={0}; point.Class=IDCMP_MOUSEMOVE; point.MouseX=G.win->MouseX; point.MouseY=G.win->MouseY; sidebar_pointer(&point); }
}

static int sugg_row_h(void)
{
    return (G.chat_font ? G.chat_font->tf_YSize : G.win->RPort->TxHeight) + 6;
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
    SetAPen(rp, p->side_dim);
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
        snprintf(hint, sizeof(hint), "Up/Down select | Return/Tab insert | Esc close");
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
            if (G.prefs.show_emoji && !G.prefs.low_memory) view_emoji(rp, p, x, y + (rh - es) / 2, G.sugg[i].emoji);
            buf_init(&c);
            buf_printf(&c, ":%s:", G.sugg[i].user);
            SetAPen(rp, i == G.sugg_sel ? p->side_sel_text : p->side_text);
            SetSoftStyle(rp, FSF_BOLD, AskSoftStyle(rp));
            Move(rp, x + es + 8, y + base);
            Text(rp, (CONST_STRPTR)buf_str(&c), (ULONG)c.len);
            SetSoftStyle(rp, 0, AskSoftStyle(rp));
            buf_free(&c);
            continue;
        }
        /* presence dot */
        SetAPen(rp, G.sugg[i].online ? p->avatar[2] : (i == G.sugg_sel ? p->side_sel_dim : p->side_dim));
        RectFill(rp, x, y + (rh - dot) / 2, x + dot - 1, y + (rh - dot) / 2 + dot - 1);
        if (!G.sugg[i].online) {
            SetAPen(rp, i == G.sugg_sel ? p->side_sel : p->side_bg);
            RectFill(rp, x + 1, y + (rh - dot) / 2 + 1, x + dot - 2, y + (rh - dot) / 2 + dot - 2);
        }
        x += dot + 8;
        buf_init(&b);
        to_latin1(&b, G.sugg[i].user, 0);
        SetAPen(rp, i == G.sugg_sel ? p->side_sel_text : p->side_text);
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
            SetAPen(rp, i == G.sugg_sel ? p->side_sel_dim : p->side_dim);
            Move(rp, x, y + base);
            Text(rp, (CONST_STRPTR)buf_str(&b), (ULONG)b.len);
        }
        if (G.sugg[i].recent) {
            static const char rec[] = "recent";
            int w = TextLength(rp, (CONST_STRPTR)rec, 6);
            SetAPen(rp, i == G.sugg_sel ? p->side_sel_dim : p->side_dim);
            Move(rp, right - 8 - w, y + base);
            Text(rp, (CONST_STRPTR)rec, 6);
        }
        buf_free(&b);
    }
    SetSoftStyle(rp, 0, AskSoftStyle(rp));
    SetBPen(rp, p->bg);
}

static void load_older(void);
static void jump_unread(void);
static void transfer_dialog(NetMsg *m);
static void download_attachment(int message, int file);
static void open_external_image(int message,int file);
static void composer_capture(void);
static void composer_set(const char *text, int cursor);

static void remember_anchor(void)
{
    int row = G.chat.top, first, m;
    if(G.server_view) return;
    if (G.anchor_id[0]) return;
    if (G.chat.follow || row < 0 || row >= G.chat.n) return;
    m = G.chat.v[row].msg_index;
    if (m < 0 || m >= G.msgs.count) return;
    first = row;
    while (first > 0 && G.chat.v[first - 1].msg_index == m) first--;
    ar_strlcpy(G.anchor_id, G.msgs.items[m].id, sizeof(G.anchor_id));
    G.anchor_line = row - first;
}

static void application_event(const char *,const char *,const char *,const char *);
static void msgs_rebuild(int scroll_bottom);
static void remember_anchor(void);
#include "inline_images.inc"

static void draw_chat(void)
{
    struct IBox box;
    struct TextFont *old_font;
    const RcMsgList *messages=(G.multi && G.server_view)?&G.servers[G.active_server].events:&G.msgs;
    if (!area_box(G.g[G_MSGS], &box))
        return;
    old_font = G.win->RPort->Font;
    if (G.chat_font) SetFont(G.win->RPort, G.chat_font);
    G.chat.unread_id=G.server_view?NULL:G.unread_anchor;
    G.chat.code_font = G.code_font;
    G.chat.no_emoji = !G.prefs.show_emoji || G.prefs.low_memory; G.chat.no_avatars = !G.prefs.show_avatars || G.prefs.low_memory || G.server_view;
    G.chat.inline_images = G.prefs.inline_images && !G.server_view;
    if(G.server_view) G.chat.nimages=0; else image_view_update();
    if (G.chat_relayout || G.chat.width != box.Width) {
        ar_ms now = amiga_local_ms();
        const char *empty = G.server_view ? "Server events - right-click the server for actions" : !G.connected ? "Not connected"
                          : !G.cur_rid[0] ? "Select a room"
                          : G.loading_rid[0] ? "Loading messages..." : "No messages yet";
        chat_layout(&G.chat, G.win->RPort, box.Width, messages, G.tz_ms, ms_day_number(now),
                    G.username, empty);
        if (G.anchor_id[0]) {
            int j;
            for (j = 0; j < G.chat.n; j++) {
                int m = G.chat.v[j].msg_index;
                if (m >= 0 && m < G.msgs.count && !strcmp(G.msgs.items[m].id, G.anchor_id)) {
                    int k = j;
                    while (k + 1 < G.chat.n && G.chat.v[k + 1].msg_index == m && k - j < G.anchor_line) k++;
                    G.chat.top = k; G.chat.follow = 0; break;
                }
            }
            G.anchor_id[0] = 0;
        }
        G.chat_relayout = 0;
    }
    chat_draw(&G.chat, G.win->RPort, &box, &G.pens);
    { int row, size = G.chat.line_h * 2 - 4;
      for (row = G.chat.top; row < G.chat.n && row < G.chat.top + G.chat.visible; row++) {
        const VLine *line = &G.chat.v[row];
        if (!G.server_view && G.prefs.show_avatars && line->kind == VL_HEADER && line->msg_index >= 0 && line->msg_index < G.msgs.count) {
            struct BitMap *bitmap = avatar_get(G.msgs.items[line->msg_index].username, size);
            int y = box.Top + (row - G.chat.top) * G.chat.line_h + 1;
            int height = box.Top + box.Height - y;
            if (height > size) height = size;
            if (bitmap && height > 0) BltBitMapRastPort(bitmap, 0, 0, G.win->RPort, box.Left + 6, y, size, height, 0xC0);
        }
      }
    }
    if (!G.server_view && G.copy_chat && !strcmp(G.copy_rid, G.cur_rid)) {
        int row;
        SetAPen(G.win->RPort, G.pens.accent);
        for (row = G.chat.top; row < G.chat.n && row < G.chat.top + G.chat.visible; row++) {
            int m = G.chat.v[row].msg_index;
            if (m >= 0 && m < G.msgs.count && !strcmp(G.msgs.items[m].id, G.copy_id)) {
                int y = box.Top + (row - G.chat.top) * G.chat.line_h;
                RectFill(G.win->RPort, box.Left, y, box.Left + 2, y + G.chat.line_h - 1);
            }
        }
    }
    set_scroller(G_CHATSCROLL, G.chat.top, G.chat.visible, G.chat.n);
    if (G.n_sugg)
        draw_suggestions(&box);
    SetFont(G.win->RPort, old_font);
    images_auto_visible();
}

static void draw_header(void)
{
    struct IBox box;
    const RcRoom *r = !G.server_view && G.cur_rid[0] ? find_room(G.cur_rid) : NULL;
    Buf t, tp;

    if (!area_box(G.g[G_ROOMHDR], &box))
        return;
    buf_init(&t);
    buf_init(&tp);
    if(G.multi && G.server_view) {
        buf_puts(&t,server_display(G.active_server));
        to_latin1(&tp,G.servers[G.active_server].connection_text,0);
    } else if (r) {
        room_label(&t, r, 0);
        to_latin1(&tp, G.topic, 0);
    } else {
        buf_puts(&t, G.connected ? "Select a room" : "AmiRocket");
    }
    if(G.status_until>amiga_local_ms() && G.status[0]){buf_clear(&tp);buf_puts(&tp,G.status);}
    if(G.draft_warned) buf_puts(&t," [draft not saved]");
    { char health[96]; if(tp.len) buf_puts(&tp," | "); buf_puts(&tp,server_health(G.active_server,health,sizeof(health))); }
    header_draw(G.win->RPort, &box, &G.pens, buf_str(&t), buf_str(&tp), r ? r->favorite : 0);
    buf_free(&t);
    buf_free(&tp);
    if (area_box(G.g[G_COMPOSELABEL], &box))
        composer_draw(G.win->RPort, &box, &G.pens, G.compose_label);
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
    if (top <= 0) load_older();
}

static int over(Object *o, WORD mx, WORD my)
{
    struct IBox b;
    return area_box(o, &b) && mx >= b.Left && mx < b.Left + b.Width && my >= b.Top && my < b.Top + b.Height;
}

static void rooms_rebuild(void);
static void update_suggestions(void);
static void redraw_dirty(void);
static void on_side_click(int x,int y);
static void on_chat_click(int x, int y);

static int is_name_char(unsigned char c);

/* Runs inside the edit hook: replaces the "@pre" word before the cursor in
 * the work buffer with the selected suggestion (already Latin-1, with a
 * trailing space) and puts the cursor after it. */
static void editor_wake(void)
{
    G.input_changed = 1;
    if (G.dirty_sig >= 0 && G.port)
        Signal((struct Task *)G.port->mp_SigTask, 1UL << G.dirty_sig);
}
static void editor_char(struct ChangeListener *l, ULONG ch) { editor_wake(); }
static void editor_change(struct ChangeListener *l, ULONG a, ULONG b, ULONG c, ULONG d) { editor_wake(); }

enum { EDITOR_NONE, EDITOR_INSERT, EDITOR_DISMISS, EDITOR_SEND, EDITOR_NEWLINE, EDITOR_NEXTROOM, EDITOR_PREVROOM, EDITOR_QUICKROOM, EDITOR_NEXTUNREAD };
static void switch_conversation(int direction);
static void quick_conversation(void);
static void next_unread(void);

static int editor_key(UWORD key, UWORD qualifier)
{
    int consumed = 0;
    if((qualifier&IEQUALIFIER_CONTROL) && key==0x42) {
        G.editor_action=(qualifier&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT))?EDITOR_PREVROOM:EDITOR_NEXTROOM;return 1;
    }
    if((qualifier&IEQUALIFIER_CONTROL) && key==0x27){G.editor_action=EDITOR_QUICKROOM;return 1;}
    if((qualifier&IEQUALIFIER_CONTROL) && key==0x16){G.editor_action=EDITOR_NEXTUNREAD;return 1;}
    if ((key == 0x44 || key == 0x43) &&
        (qualifier & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND))) {
        G.editor_action = EDITOR_SEND; consumed = 1;
    } else if (key == 0x44 || key == 0x43) {
        G.editor_action = (qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
                          ? EDITOR_NEWLINE : G.n_sugg ? EDITOR_INSERT : EDITOR_SEND;
        consumed = 1;
    } else if (G.n_sugg) {
        if (key == 0x4C) { G.sugg_move--; consumed = 1; }
        else if (key == 0x4D) { G.sugg_move++; consumed = 1; }
        else if (key == 0x42) { G.editor_action = EDITOR_INSERT; consumed = 1; }
        else if (key == 0x45) { G.editor_action = EDITOR_DISMISS; consumed = 1; }
    }
    return consumed;
}

/* texteditor consumes arrows before window.class can report RAWKEY.
 * Intercept composer and popup keys; defer all UI changes to our task. */
static ULONG editor_dispatch(struct IClass *cl, Object *o, Msg msg)
{
    if (msg->MethodID == GM_HANDLEINPUT) {
        struct gpInput *gp = (struct gpInput *)msg;
        const struct InputEvent *ie = gp->gpi_IEvent;
        if (ie && ie->ie_Class == IECLASS_RAWKEY && !(ie->ie_Code & IECODE_UP_PREFIX)) {
            if (editor_key(ie->ie_Code, ie->ie_Qualifier)) { editor_wake(); return GMR_MEACTIVE; }
        }
    }
    return DoSuperMethodA(cl, o, msg);
}

static ULONG filter_hook_func(struct Hook *h, struct SGWork *sgw, ULONG *msg)
{
    if (*msg != SGH_KEY) return 0;
    G.filter_changed = 1;
    if (G.dirty_sig >= 0) Signal((struct Task *)G.port->mp_SigTask, 1UL << G.dirty_sig);
    return ~0UL;
}

static void composer_capture(void)
{
    STRPTR text;
    ULONG x = 0, y = 0;
    int pos = 0;
    if (!G.win || !G.g[G_INPUT]) return;
    text = (STRPTR)DoGadgetMethod((struct Gadget *)G.g[G_INPUT], G.win, NULL, GM_TEXTEDITOR_ExportText, NULL);
    if (!text) return;
    G.composer_overlong = strlen((char *)text) > INPUT_MAX;
    ar_strlcpy(G.hook_buf, text, sizeof(G.hook_buf));
    FreeVec(text);
    GetAttr(GA_TEXTEDITOR_CursorX, G.g[G_INPUT], &x);
    GetAttr(GA_TEXTEDITOR_CursorY, G.g[G_INPUT], &y);
    while (y && G.hook_buf[pos]) if (G.hook_buf[pos++] == '\n') y--;
    while (x && G.hook_buf[pos] && G.hook_buf[pos] != '\n') { pos++; x--; }
    G.hook_pos = pos;
}

static void composer_set(const char *text, int cursor)
{
    int i, x = 0, y = 0;
    SetGadgetAttrs((struct Gadget *)G.g[G_INPUT], G.win, NULL,
                   GA_TEXTEDITOR_Contents, (ULONG)text, TAG_END);
    for (i = 0; i < cursor && text[i]; i++) { if (text[i] == '\n') { y++; x = 0; } else x++; }
    SetGadgetAttrs((struct Gadget *)G.g[G_INPUT], G.win, NULL,
                   GA_TEXTEDITOR_CursorY, y, GA_TEXTEDITOR_CursorX, x, TAG_END);
    ar_strlcpy(G.hook_buf, text, sizeof(G.hook_buf)); G.hook_pos = cursor;
}

static void cmd_send_input(void);
static void complete_suggestion(int i);
static void hide_suggestions(void);

static void input_events(void)
{
    if (G.editor_action || G.sugg_move) {
        int action = G.editor_action, move = G.sugg_move;
        G.editor_action = EDITOR_NONE; G.sugg_move = 0; G.input_changed = 0;
        if (G.n_sugg && move) {
            G.sugg_sel = ((G.sugg_sel + move) % G.n_sugg + G.n_sugg) % G.n_sugg;
            mark_dirty(DIRTY_CHAT);
        }
        if (action == EDITOR_INSERT && G.n_sugg) { composer_capture(); complete_suggestion(G.sugg_sel); }
        else if (action == EDITOR_DISMISS) hide_suggestions();
        else if (action == EDITOR_SEND) cmd_send_input();
        else if(action==EDITOR_NEXTROOM) switch_conversation(1);
        else if(action==EDITOR_PREVROOM) switch_conversation(-1);
        else if(action==EDITOR_QUICKROOM)quick_conversation();
        else if(action==EDITOR_NEXTUNREAD)next_unread();
        else if (action == EDITOR_NEWLINE) {
            hide_suggestions();
            DoGadgetMethod((struct Gadget *)G.g[G_INPUT], G.win, NULL,
                           GM_TEXTEDITOR_InsertText, NULL, "\n", GV_TEXTEDITOR_InsertText_Cursor);
            editor_wake();
        }
    }
    if (G.filter_changed && G.win) {
        STRPTR value = NULL;
        G.filter_changed = 0;
        GetAttr(STRINGA_TextVal, G.g[G_FILTER], (ULONG *)&value);
        ar_strlcpy(G.filter, value ? (char *)value : "", sizeof(G.filter));
        G.side.top = 0; rooms_rebuild();
    }
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
        composer_capture();
        update_suggestions();
        if (G.n_sugg)
            mark_dirty(DIRTY_CHAT);   /* the hook may have moved the selection */
    }
}

/* IDCMP hook: live scroller dragging and the mouse wheel. Runs in our task
 * from inside RA_HandleInput(). */
static ULONG idcmp_hook_func(struct Hook *h, APTR obj, struct IntuiMessage *im)
{
    sidebar_pointer(im);
    if ((im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN) ||
        im->Class == IDCMP_GADGETDOWN) {
        struct IBox box;
        /* Layout may own the IntuiMessage.IAddress for a nested gadget.
         * Match the drawn bounds, not that pointer. Read-only spaces also
         * allow ordinary window mouse-button events on OS 3.x. */
        if (over(G.g[G_INPUT], im->MouseX, im->MouseY)) { G.copy_chat = 0; mark_dirty(DIRTY_CHAT); }
        if (over(G.g[G_ROOMS], im->MouseX, im->MouseY) && area_box(G.g[G_ROOMS], &box))
            on_side_click(im->MouseX-box.Left,im->MouseY - box.Top);
        else if (over(G.g[G_MSGS], im->MouseX, im->MouseY) && area_box(G.g[G_MSGS], &box))
            on_chat_click(im->MouseX - box.Left, im->MouseY - box.Top);
        redraw_dirty();
    } else if (im->Class == IDCMP_IDCMPUPDATE) {
        struct TagItem *tags = (struct TagItem *)im->IAddress;
        struct TagItem *ti = FindTagItem(GA_ID, tags), *top = FindTagItem(SCROLLER_Top, tags);
        if (ti && ti->ti_Data == G_INPUT) {
            editor_wake();
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

static int room_matches(const RcRoom *r)
{
    Buf label;
    const char *p;
    size_t n = strlen(G.filter);
    int found = !n;
    if (found) return 1;
    buf_init(&label); room_label(&label, r, 0);
    for (p = buf_str(&label); *p; p++)
        if (!ar_strncasecmp(p, G.filter, n)) { found = 1; break; }
    buf_free(&label); return found;
}

#include "draft_runtime.inc"
#include "server_context.inc"
#include "server_tree.inc"
#include "dock_state.inc"

static void rooms_rebuild(void)
{
    int *order, i, s;
    Buf b;

    if(G.multi) { tree_rebuild(); return; }
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
            if (room_section(r) == s && room_matches(r))
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
        if (G.collapsed[s] && !G.filter[0])
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
            row->participants = (WORD)rc_dm_participants(r, G.username);
            row->is_dm = r->type == 'd';
            if (row->is_dm && !row->participants)
                ar_strlcpy(row->avatar_user, r->name, sizeof(row->avatar_user));
            row->selected = !strcmp(r->rid, G.cur_rid);
            row->mentions=r->mentions>999?999:r->mentions;
            row->bold = r->unread > 0 || r->alert || r->mentions;
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
    if (scroll_bottom) {
        G.anchor_id[0] = 0;
        G.chat.follow = 1;
    }
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

    if (!G.win || !G.connected || !G.cur_rid[0] || G.composer_overlong) {
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
    if (at == 0 || (txt[at - 1] != '@' && txt[at - 1] != ':') || (at >= 2 && txt[at - 2] != ' ' && txt[at - 2] != '\n')) {
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
        composer_set(buf_str(&b), cursor);
        ar_strlcpy(G.hook_buf, buf_str(&b), sizeof(G.hook_buf));
        G.hook_pos = cursor;
    }
    buf_free(&b);
    buf_free(&u);
    hide_suggestions();
    ActivateLayoutGadget((struct Gadget *)G.g[0], G.win, NULL, (ULONG)G.g[G_INPUT]);

}

static int shell_argument(Buf *cmd, const char *text)
{
    const unsigned char *p;
    if (!text || !*text) return 0;
    for (p = (const unsigned char *)text; *p; p++) if (*p < 32 || *p == 127) return 0;
    buf_putc(cmd, '"');
    for (p = (const unsigned char *)text; *p; p++) { if (*p == '*' || *p == '"') buf_putc(cmd, '*'); buf_putc(cmd, *p); }
    buf_putc(cmd, '"'); return !cmd->oom;
}

/* Background shells must own their streams. Otherwise their exit can close
 * the client's inherited CLI handles after Preferences Use/Save returns. */
static LONG launch_helper(const char *command)
{
    BPTR input=Open("NIL:",MODE_OLDFILE), output=Open("NIL:",MODE_NEWFILE); LONG rc;
    if(!input || !output) { if(input) Close(input); if(output) Close(output); return -1; }
    rc=SystemTags(command,SYS_Asynch,TRUE,SYS_UserShell,TRUE,SYS_Input,input,SYS_Output,output,NP_StackSize,131072,TAG_END);
    if(rc<0) { Close(input); Close(output); }
    return rc;
}

/* Pass only HTTP(S) URLs as a single escaped Amiga shell argument. */
static int web_link_command(Buf *cmd, const char *url)
{
    const unsigned char *p;
    if (ar_strncasecmp(url, "http://", 7) && ar_strncasecmp(url, "https://", 8)) return 0;
    if (strlen(url) > 4096) return 0;
    for (p = (const unsigned char *)url; *p; p++) if (*p <= 32 || *p == 127) return 0;
    if (!shell_argument(cmd, G.prefs.browser)) return 0;
    buf_puts(cmd, " \"");
    for (p = (const unsigned char *)url; *p; p++) {
        if (*p == '*' || *p == '"') buf_putc(cmd, '*');
        buf_putc(cmd, *p);
    }
    buf_putc(cmd, '"'); return !cmd->oom;
}

static void open_web_link(const char *url)
{
    Buf cmd; BPTR lock;
    buf_init(&cmd);
    if (!G.prefs.open_links) { set_status("Web links disabled in Plugins preferences"); return; }
    if (!web_link_command(&cmd, url)) { buf_free(&cmd); set_status("Cannot open this link"); return; }
    lock = Lock(G.prefs.browser, ACCESS_READ);
    if (!lock) request("Open web link", "Configure an installed browser helper in AmiRocketPrefs / Plugins. OpenURL also needs a configured Amiga web browser.");
    else {
        UnLock(lock);
        if (launch_helper(buf_str(&cmd)) < 0) request("Open web link", "OpenURL could not be started.");
        else set_status("Opening link in web browser...");
    }
    buf_free(&cmd);
}

static NetMsg *inline_reaction_command(int message,int index)
{
    const char *p;const RcMsg *target;char code[64];NetMsg *m;int length;
    if(G.inline_reaction_pending || !G.connected || message<0 || message>=G.msgs.count)return NULL;
    target=&G.msgs.items[message];p=target->reactions;
    while(p && index-- >0) {p=strchr(p,'\n');if(p)p++;}
    if(!p)return NULL;
    length=strcspn(p," \n");if(length<=0 || length>=(int)sizeof(code))return NULL;
    memcpy(code,p,length);code[length]=0;m=netmsg_new(NC_REACT);if(!m)return NULL;
    m->a_rid=ar_strdup(G.cur_rid);m->a_uid=ar_strdup(target->id);m->a_text=ar_strdup(code);m->a_interval=-1;
    m->a_verify=!rc_reaction_has(target,code,G.username);
    if(!m->a_rid || !m->a_uid || !m->a_text){netmsg_free(m);return NULL;}
    return m;
}
static void inline_reaction_click(int message,int index)
{
    NetMsg *command=inline_reaction_command(message,index);
    if(command) {G.inline_reaction_pending=1;send_cmd(command);}
}
static void on_chat_click(int x, int y)
{
    struct IBox box;
    int rh, rows, top, i;

    if (!area_box(G.g[G_MSGS], &box)) return;
    if(G.server_view) return;
    if (!G.n_sugg) {
        int reaction,row=G.chat.line_h>0?G.chat.top+y/G.chat.line_h:-1;struct TextFont *old=G.win->RPort->Font;
        if(G.chat_font)SetFont(G.win->RPort,G.chat_font);
        reaction=chat_reaction_hit(&G.chat,G.win->RPort,x,y);SetFont(G.win->RPort,old);
        if(reaction>=0 && row>=0 && row<G.chat.n) {inline_reaction_click(G.chat.v[row].msg_index,reaction);return;}
        const char *url = chat_link_hit(&G.chat, x, y);
        if (url) { open_web_link(url); return; }
        int message, file, file_row = G.chat.line_h > 0 ? G.chat.top + y / G.chat.line_h : -1;
        row=file_row;
        if (y >= 0 && row >= 0 && row < G.chat.n) {
            message = G.chat.v[row].msg_index;
            if (message >= 0 && message < G.msgs.count) {
                ar_strlcpy(G.copy_id, G.msgs.items[message].id, sizeof(G.copy_id));
                ar_strlcpy(G.copy_rid, G.cur_rid, sizeof(G.copy_rid));
                G.copy_chat = 1; mark_dirty(DIRTY_CHAT);
                set_status("Message selected - Amiga+C copies, Amiga+T actions");
            }
        }
        if (chat_file_hit(&G.chat, y, &message, &file)) {
            G.copy_file = file;
            struct TextFont *font=G.win->RPort->Font; int action;
            if(G.chat_font)SetFont(G.win->RPort,G.chat_font);
            action=chat_file_action(&G.chat,G.win->RPort,x,y);SetFont(G.win->RPort,font);
            if(row>=0 && row<G.chat.n && G.chat.v[row].kind==VL_FILE_ACTION && action<0)return;
            if(action==2) download_attachment(message,file);
            else if(action==1 || (row>=0 && row<G.chat.n && G.chat.v[row].kind==VL_IMAGE)) {
                if(G.msgs.items[message].files[file].image)open_external_image(message,file);
                else download_attachment(message,file);
            } else if(G.msgs.items[message].files[file].image && G.prefs.inline_images)image_toggle(message,file);
            else if(G.msgs.items[message].files[file].image)open_external_image(message,file);
            else download_attachment(message,file);
        }
        return;
    }
    rh = sugg_row_h();
    rows = G.n_sugg + 1;
    top = box.Height - rows * rh - 2;
    i = (y - top) / rh - 1;
    if (y >= top && i >= 0 && i < G.n_sugg)
        complete_suggestion(i);
    else
        hide_suggestions();
}

static void on_side_click(int x,int y)
{
    struct IBox box;
    int i;

    if (!area_box(G.g[G_ROOMS], &box))
        return;
    if (y < 0 || y >= box.Height)
        return;
    i = side_hit(&G.side, y);
    if (i < 0)
        return;
    if(G.multi) {
        SRow row=G.side.r[i]; char rid[64]; int slot=row.server_slot;
        if(row.kind==SR_SERVER) {
            int expanded=G.servers[slot].expanded;
            if(slot!=G.active_server) server_select(slot);
            if(x<24) G.servers[slot].expanded=!expanded;
            G.server_view=1; G.copy_chat=0; G.n_sugg=0;
            rooms_rebuild(); msgs_rebuild(1); update_title(); return;
        }
        if(row.kind==SR_SECTION) { G.servers[slot].collapsed[row.section]=!G.servers[slot].collapsed[row.section]; rooms_rebuild(); return; }
        ar_strlcpy(rid,row.rid?row.rid:"",sizeof(rid));
        if(slot!=G.active_server) { server_select(slot); rooms_rebuild(); if(!rid[0]) return;
            for(i=0;i<G.side.n;i++) if(G.side.r[i].server_slot==slot && G.side.r[i].rid && !strcmp(G.side.r[i].rid,rid)) break;
            if(i==G.side.n) return;
        }
    }
    if (G.side.r[i].kind == SR_SECTION) {
        int s = G.side.r[i].section;
        G.collapsed[s] = !G.collapsed[s];
        rooms_rebuild();
        return;
    }
    if(G.side.r[i].kind!=SR_ROOM || !G.side.r[i].rid) return;
    if(G.server_view) { G.server_view=0; G.chat.width=0; msgs_rebuild(0); update_title(); }
    if(!strcmp(G.side.r[i].rid,G.cur_rid)) return;
    ar_strlcpy(G.cur_rid, G.side.r[i].rid, sizeof(G.cur_rid));
    G.topic[0] = 0;
    G.history_pending = G.history_exhausted = 0;
    G.anchor_id[0] = 0;
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
    m->server_slot=G.active_server;
    m->server_generation=G.multi ? G.servers[G.active_server].generation : 0;
    G.pending++;
    worker_send(m);
}

static void load_older(void)
{
    NetMsg *m;
    if (G.server_view || !G.connected || !G.cur_rid[0] || G.loading_rid[0] || G.history_pending || G.history_exhausted || !G.msgs.count) return;
    if (G.msgs.count >= MAX_MESSAGES) { set_status("Message retention limit reached; adjust Preferences / Memory to load more"); return; }
    m = netmsg_new(NC_HISTORY); if (!m) return;
    m->a_rid = ar_strdup(G.cur_rid);
    m->a_before = G.msgs.items[0].ts_ms;
    m->a_interval = G.room_serial;
    if (!m->a_rid) { netmsg_free(m); return; }
    G.history_pending = 1;
    set_status("Loading older messages..."); send_cmd(m);
}

static void on_older(NetMsg *m)
{
    ar_ms earliest;
    int count;
    if (strcmp(m->rid, G.cur_rid) || m->a_interval != G.room_serial) return;
    G.history_pending = 0;
    if (m->rc != RC_OK) { G.unread_seek=0;set_status_utf8(m->text[0] ? m->text : rc_strerror(m->rc)); return; }
    earliest = G.msgs.count ? G.msgs.items[0].ts_ms : 0;
    count = m->msgs.count;
    remember_anchor();
    rc_msglist_merge(&G.msgs, &m->msgs, MAX_MESSAGES);
    G.history_exhausted = count < 50 || (G.msgs.count && G.msgs.items[0].ts_ms >= earliest);
    unread_resolve();
    msgs_rebuild(0);
    set_status(G.history_exhausted ? "Beginning of room history" : "Older messages loaded - scroll up for more");
    if(G.unread_seek)jump_unread();
}

static void download_attachment(int message, int file)
{
    const RcAttachment *a;
    struct FileRequester *fr;
    char name[128], path[512],save_title[128],file_size[48];
    Buf title;
    NetMsg *m;
    int i, j = 0;
    if (G.download_pending) { set_status("An attachment is already downloading"); return; }
    if (message < 0 || message >= G.msgs.count || file < 0 || file >= G.msgs.items[message].file_count) return;
    a = &G.msgs.items[message].files[file];
    if(view_attachment_limit(a,0)) { request("Attachment too large",view_attachment_limit(a,0)); return; }
    buf_init(&title); to_latin1(&title, a->title, 0);
    for (i = 0; buf_str(&title)[i] && j < (int)sizeof(name) - 1; i++) {
        unsigned char c = (unsigned char)buf_str(&title)[i];
        if (c >= 32 && c != ':' && c != '/' && c != '\\' && c != '\"' && c != '*') name[j++] = (char)c;
    }
    name[j] = 0; buf_free(&title);
    if (!name[0] || !strcmp(name, ".") || !strcmp(name, "..")) strcpy(name, "attachment");
    view_file_size(file_size,sizeof(file_size),a->size);
    snprintf(save_title,sizeof(save_title),"Save attachment (%s; maximum 8 MiB)",file_size);
    {BPTR drawer=CreateDir(G.prefs.download_dir);if(drawer)UnLock(drawer);}
    fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_Window, (ULONG)G.win,
            ASLFR_TitleText, (ULONG)save_title, ASLFR_DoSaveMode, TRUE,
            ASLFR_InitialDrawer,(ULONG)G.prefs.download_dir, ASLFR_InitialFile, (ULONG)name, TAG_END);
    if (!fr) return;
    if (!AslRequestTags(fr, TAG_END)) { FreeAslRequest(fr); return; }
    ar_strlcpy(path, fr->fr_Drawer, sizeof(path));
    if (!AddPart(path, fr->fr_File, sizeof(path)) || !fr->fr_File[0] || strpbrk(path, "\r\n")) {
        FreeAslRequest(fr); request("AmiRocket", "Invalid or overly long filename"); return;
    }
    FreeAslRequest(fr);
    { BPTR existing = Lock(path, ACCESS_READ);
      if (existing) {
          struct EasyStruct es = { sizeof(es), 0, (STRPTR)"Replace attachment", (STRPTR)"Replace the existing file?", (STRPTR)"Replace|Cancel" };
          UnLock(existing); if (EasyRequest(G.win, &es, NULL) != 1) return;
      }
    }
    m = netmsg_new(NC_DOWNLOAD); if (!m) return;
    m->a_url = ar_strdup(a->url); m->a_text = ar_strdup(path);
    m->a_verify = a->image && G.prefs.preview_images;
    if(G.prefs.low_memory) m->a_before=2UL*1024*1024;
    if (!m->a_url || !m->a_text) { netmsg_free(m); return; }
    transfer_dialog(m);
}

#include "external_images.inc"

static void on_downloaded(NetMsg *m)
{
    G.download_pending = 0;
    if(G.transfer_dialog) { G.transfer_rc=m->rc; ar_strlcpy(G.transfer_status,m->text[0]?m->text:rc_strerror(m->rc),sizeof(G.transfer_status)); }
    if (m->rc != RC_OK) { if(G.transfer_dialog) return; request_utf8("Attachment download failed", m->text[0] ? m->text : rc_strerror(m->rc)); return; }
    set_status(m->text[0] ? m->text : "Attachment saved");
    if(m->a_verify==2) G.external_pending=0;
    if(m->a_verify) launch_image_viewer(m->a_text);
}

static void load_chat_font(void)
{
    struct TextFont *font = NULL;
    if (G.prefs.chat_font[0]) {
        struct TextAttr ta;
        ta.ta_Name = (STRPTR)G.prefs.chat_font; ta.ta_YSize = (UWORD)G.prefs.chat_font_size;
        ta.ta_Style = FS_NORMAL; ta.ta_Flags = FPF_DISKFONT;
        font = OpenDiskFont(&ta);
    }
    if (G.chat_font) CloseFont(G.chat_font);
    G.chat_font = font;
    if (G.code_font) CloseFont(G.code_font);
    { struct TextAttr ta = { (STRPTR)"courier.font", (UWORD)(font ? font->tf_YSize : 13), FS_NORMAL, FPF_DISKFONT };
      G.code_font = OpenDiskFont(&ta);
      if (G.code_font && (G.code_font->tf_Flags & FPF_PROPORTIONAL)) { CloseFont(G.code_font); G.code_font = NULL; }
      if (!G.code_font) { ta.ta_Name = (STRPTR)"topaz.font"; ta.ta_YSize = 8; ta.ta_Flags = 0; G.code_font = OpenFont(&ta); }
    }
    mark_dirty(DIRTY_SIDE);
}

static void __attribute__((unused)) choose_chat_font(void)
{
    struct FontRequester *fr = AllocAslRequestTags(ASL_FontRequest,
        ASLFO_Window, (ULONG)G.win, ASLFO_TitleText, (ULONG)"Chat font",
        ASLFO_InitialName, (ULONG)(G.prefs.chat_font[0] ? G.prefs.chat_font : "helvetica.font"),
        ASLFO_InitialSize, G.prefs.chat_font_size, ASLFO_MinHeight, 8, ASLFO_MaxHeight, 48,
        ASLFO_DoStyle, FALSE, ASLFO_DoDrawMode, FALSE, TAG_END);
    if (!fr) return;
    if (AslRequestTags(fr, TAG_END)) {
        remember_anchor();
        ar_strlcpy(G.prefs.chat_font, fr->fo_Attr.ta_Name, sizeof(G.prefs.chat_font));
        G.prefs.chat_font_size = fr->fo_Attr.ta_YSize;
        load_chat_font(); prefs_save_client_state(); msgs_rebuild(0);
    }
    FreeAslRequest(fr);
}

static int conversation_visible(void)
{
    return G.win && (G.win->Flags&WFLG_WINDOWACTIVE) && !G.server_view && G.connected && G.cur_rid[0] && !G.loading_rid[0];
}
static void sync_conversation_focus(void)
{
    int visible=conversation_visible(); NetMsg *m;
    if(!G.running_client) return;
    if(G.focus_slot==G.active_server && G.focus_visible==visible && !strcmp(G.focus_rid,G.cur_rid)) return;
    m=netmsg_new(NC_SET_FOCUS); if(!m) return;
    m->a_interval=visible; m->a_rid=ar_strdup(G.cur_rid);
    G.focus_slot=G.active_server; G.focus_visible=visible; ar_strlcpy(G.focus_rid,G.cur_rid,sizeof(G.focus_rid));
    send_cmd(m);
    if(visible) { int i; for(i=0;i<G.rooms.count;i++) if(!strcmp(G.rooms.items[i].rid,G.cur_rid)) G.rooms.items[i].unread=G.rooms.items[i].alert=G.rooms.items[i].mentions=0; rooms_rebuild(); notify_unread(); }
}

#include "unread.inc"

static void cmd_open_room(const char *rid)
{
    G.unread_seek=0;
    if(!draft_bind(rid)) {
        const char *old=strrchr(G.draft_key,'\n');
        request("Draft kept","Could not retain the draft safely. Copy, shorten or clear it before switching conversations.");
        if(old && old[1]) { ar_strlcpy(G.cur_rid,old+1,sizeof(G.cur_rid)); cmd_open_room(G.cur_rid); }
        return;
    }
    if(G.running_client) prefs_save_client_state();
    unread_capture(rid);
    ar_strlcpy(G.loading_rid,rid,sizeof(G.loading_rid));
    sync_conversation_focus();
    NetMsg *m = netmsg_new(NC_OPEN_ROOM);
    if (!m)
        return;
    m->a_rid = ar_strdup(rid);
    m->a_interval = ++G.room_serial;
    G.history_pending = G.history_exhausted = 0;
    G.anchor_id[0] = 0;
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

#include "send_recovery.inc"
#include "navigation_polish.inc"
static void cmd_send_input(void)
{
    STRPTR txt = NULL;
    NetMsg *m;
    Buf b;
    char *t;

    if (G.send_pending || G.server_view) return;
    composer_capture();
    { STRPTR all = (STRPTR)DoGadgetMethod((struct Gadget *)G.g[G_INPUT], G.win, NULL, GM_TEXTEDITOR_ExportText, NULL);
      if (!all) return;
      if (strlen((char *)all) > INPUT_MAX) { FreeVec(all); request("AmiRocket", "Message is too long (maximum 4000 characters)."); return; }
      FreeVec(all);
    }
    txt = (STRPTR)G.hook_buf;
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
    if(!m->a_rid) { netmsg_free(m); set_status("Not sent: out of memory; draft kept"); return; }
    if(!send_record_prepare(m)){netmsg_free(m);set_status("Not sent: delivery record or draft could not be saved, or retry cancelled");send_state_refresh();return;}
    G.send_pending = 1;send_state_refresh();
    send_cmd(m);
    ActivateLayoutGadget((struct Gadget *)G.g[0], G.win, NULL, (ULONG)G.g[G_INPUT]);
}

/* ---- login window -------------------------------------------------- */

static int text_xy_index(const char *text, ULONG x, ULONG y)
{
    int i = 0; ULONG row = 0, col = 0;
    while (text[i] && row < y) { if (text[i++] == '\n') row++; }
    while (text[i] && text[i] != '\n' && col < x) { i++; col++; }
    return i;
}
static void toolbar_format(const char *before, const char *after, const char *placeholder)
{
    ULONG sx = ~0UL, sy = ~0UL, ex = ~0UL, ey = ~0UL; int start, end; Buf b;
    composer_capture(); if (G.composer_overlong) return;
    start = end = G.hook_pos;
    DoGadgetMethod((struct Gadget *)G.g[G_INPUT], G.win, NULL, GM_TEXTEDITOR_BlockInfo, NULL, &sx, &sy, &ex, &ey);
    if (sx != ~0UL && sy != ~0UL && ex != ~0UL && ey != ~0UL) {
        int a = text_xy_index(G.hook_buf, sx, sy), z = text_xy_index(G.hook_buf, ex, ey);
        if (a != z) { start = a < z ? a : z; end = a > z ? a : z; }
    }
    buf_init(&b); buf_append(&b, G.hook_buf, start); buf_puts(&b, before);
    if (end > start) buf_append(&b, G.hook_buf + start, end - start); else buf_puts(&b, placeholder);
    buf_puts(&b, after); buf_puts(&b, G.hook_buf + end);
    if (!b.oom && b.len <= INPUT_MAX) { composer_set(buf_str(&b), start + strlen(before)); editor_wake(); }
    else set_status("Message would exceed 4000 characters");
    buf_free(&b); G.copy_chat = 0;
    ActivateLayoutGadget((struct Gadget *)G.g[0], G.win, NULL, (ULONG)G.g[G_INPUT]);
}

static void apply_preferences(const Prefs *next,int permanent);
static long plugin_get(void *context,const char *key,char *out,long size)
{
    char full[96]; if(!key || strlen(key)>31) return -1;
    snprintf(full,sizeof(full),"plugin.%s.%s",(const char *)context,key);
    return prefs_get(&G.prefs,full,out,size);
}
static long plugin_set(void *context,const char *key,const char *value)
{
    char full[96]; if(!key || strlen(key)>31) return -1;
    snprintf(full,sizeof(full),"plugin.%s.%s",(const char *)context,key);
    if(prefs_set(&G.prefs,full,value)<0) return -1;
    G.session_override=1; G.plugins_dirty=1; prefs_service_publish(G.prefs_service,&G.prefs); return 0;
}
static long plugin_insert(void *context,const char *text)
{
    Buf b; int pos; (void)context;
    if(!G.win || !text) return -1;
    composer_capture(); if(G.composer_overlong) return -1;
    pos=G.hook_pos; buf_init(&b); buf_append(&b,G.hook_buf,pos); buf_puts(&b,text); buf_puts(&b,G.hook_buf+pos);
    if(b.oom || b.len>INPUT_MAX) { buf_free(&b); return -1; }
    composer_set(buf_str(&b),pos+strlen(text)); buf_free(&b); editor_wake(); return 0;
}
static long plugin_selection(void *context,char *out,long size)
{
    int i; (void)context; if(!out || size<1) return -1; out[0]=0;
    if(G.copy_chat && !strcmp(G.copy_rid,G.cur_rid)) for(i=0;i<G.msgs.count;i++) if(!strcmp(G.msgs.items[i].id,G.copy_id)) {
        Buf plain,latin; buf_init(&plain); buf_init(&latin); md_to_plain(&plain,G.msgs.items[i].text);
        utf8_to_latin1(&latin,buf_str(&plain),1); ar_strlcpy(out,buf_str(&latin),size); buf_free(&plain); buf_free(&latin); return 0;
    }
    composer_capture(); ar_strlcpy(out,G.hook_buf,size); return 0;
}
static void plugin_time(void *context,char *out,long size)
{
    char hm[8]; (void)context; ms_format_hm(amiga_local_ms(),hm); snprintf(out,size,"[%s] ",hm);
}
static void plugin_status(void *context,const char *text) { (void)context; set_status(text); }
static void publish_plugins(void)
{
    char ids[272]="|"; int i;
    for(i=0;i<plugins_count();i++) { size_t n=strlen(ids); snprintf(ids+n,sizeof(ids)-n,"%s|",plugins_id(i)); }
    prefs_service_plugins(G.prefs_service,ids);
}
static void plugins_start_host(void)
{
    ArPluginHost host={AR_PLUGIN_ABI,sizeof(ArPluginHost),NULL,plugin_get,plugin_set,plugin_insert,plugin_selection,plugin_time,plugin_status};
    plugins_init(&host); plugins_sync(&G.prefs); publish_plugins();
}
static void application_event(const char *kind,const char *room,const char *user,const char *text)
{
    plugins_event(kind,room,user,text); arexx_event(kind,room,user,text);
}

static int notification_mention(const char *text,const char *name)
{
    char key[104]; const char *p=text; size_t n;
    if(!text || !name || !*name) return 0;
    snprintf(key,sizeof(key),"@%s",name); n=strlen(key);
    while((p=strstr(p,key))) {
        unsigned char after=p[n],before=p==text?' ':p[-1];
        if((before==' ' || before=='\n' || before=='\t' || before=='(') &&
           !(after>='a'&&after<='z') && !(after>='A'&&after<='Z') && !(after>='0'&&after<='9') && after!='_' && after!='-' && after!='.') return 1;
        p+=n;
    }
    return 0;
}
static int notification_allowed(int slot,const char *rid,const char *user,const char *text)
{
    const ServerPrefs *p=&G.prefs.servers[slot]; const char *me=slot==G.active_server?G.username:G.servers[slot].username;
    (void)rid;
    if(p->quiet || (user && me[0] && !strcmp(user,me))) return 0;
    return !p->notify_mentions || notification_mention(text,me) || notification_mention(text,"all") || notification_mention(text,"here");
}
static void notification_sound_clear(void)
{
    if(G.notification_sound) {DoMethod(G.notification_sound,DTM_TRIGGER,NULL,STM_STOP,NULL);DisposeDTObject(G.notification_sound);}
    G.notification_sound=NULL;G.notification_sound_path[0]=0;
}
static int notification_sound_play(const char *path)
{
    char resolved[512];
    if(path && !strncmp(path,"PROGDIR:",8) && prefs_extra(&G.prefs,"workspace.data_dir","")[0]) {
        if(prefs_resource_path(&G.prefs,path,resolved,sizeof(resolved))<0)return 0;
        path=resolved;
    } else if(path && !strncmp(path,"PROGDIR:",8) && G.prefs_only) {
        BPTR local=Lock(path,ACCESS_READ);
        if(local)UnLock(local);
        else {
            ar_strlcpy(resolved,prefs_extra(&G.prefs,"workspace.install_dir","SYS:Utilities"),sizeof(resolved));
            if(!AddPart(resolved,path+8,sizeof(resolved)))return 0;
            path=resolved;
        }
    }
    if(!path || !*path || !DataTypesBase) {DisplayBeep(G.scr);return 1;}
    if(!G.notification_sound || strcmp(path,G.notification_sound_path)) {
        BPTR lock=Lock(path,ACCESS_READ);struct FileInfoBlock *info=AllocDosObject(DOS_FIB,NULL);int valid=0;
        if(lock && info && Examine(lock,info) && info->fib_DirEntryType<0 && info->fib_Size>0 && info->fib_Size<=256*1024)valid=1;
        if(lock)UnLock(lock);
        if(info)FreeDosObject(DOS_FIB,info);
        notification_sound_clear();
        if(valid)G.notification_sound=NewDTObject((APTR)path,DTA_SourceType,DTST_FILE,DTA_GroupID,GID_SOUND,SDTA_Volume,64,SDTA_Cycles,1,TAG_END);
        if(!G.notification_sound) {DisplayBeep(G.scr);return 0;}
        ar_strlcpy(G.notification_sound_path,path,sizeof(G.notification_sound_path));
    }
    return DoMethod(G.notification_sound,DTM_TRIGGER,NULL,STM_PLAY,NULL)!=0;
}
static void server_message_event(int slot,const char *rid,const char *user,const char *text)
{
    char qualified[84]; snprintf(qualified,sizeof(qualified),"%d:%s",slot+1,rid);
    if(notification_allowed(slot,rid,user,text)) {
        application_event("SERVERMESSAGE",qualified,user,text);
        if(G.prefs.servers[slot].sound && !(slot==G.active_server && conversation_visible()) && amiga_local_ms()-G.notification_sound_at>=2000) {G.notification_sound_at=amiga_local_ms();notification_sound_play(G.prefs.notification_sound);}
    }
}


static void notify_unread(void)
{
    int total=dock_total_unread(); char value[24];
    notify_dock_state();
    if(total!=G.last_unread) { G.last_unread=total; snprintf(value,sizeof(value),"%d",total); application_event("UNREAD",G.cur_rid,G.username,value); }
}

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
    if(G.multi && G.prefs.servers[G.active_server].url[0] && strcmp(server,G.prefs.servers[G.active_server].url)) {
        request("Server","Edit server addresses in Preferences / Servers, or add another server there."); return;
    }
    if(G.multi && strcmp(user,G.prefs.user)) {
        G.servers[G.active_server].generation++; G.prefs.token[0]=G.prefs.user_id[0]=0;
        avatar_reset(); images_reset(); rc_roomlist_free(&G.rooms); rc_msglist_free(&G.msgs);
        G.cur_rid[0]=G.loading_rid[0]=G.topic[0]=0; G.connected=0;
    }
    ar_strlcpy(G.prefs.server, server, sizeof(G.prefs.server));
    ar_strlcpy(G.prefs.user, user, sizeof(G.prefs.user));
    G.prefs.verify = checked(G.g[L_VERIFY]);
    G.prefs.remember = checked(G.g[L_REMEMBER]);
    server_legacy_save();

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
    server_event(G.active_server,0,"Connecting...");
    set_status("Connecting...");
    set_busy(1);
    send_cmd(m);
}

/* ---- worker results ------------------------------------------------ */

static void forget_login(void)
{
    G.prefs.token[0] = 0;
    G.prefs.user_id[0] = 0;
    prefs_save_client_state();
}

static void went_offline(const char *why)
{
    avatar_reset(); images_reset();
    G.presence_user_id[0] = 0;
    G.connected = 0; G.server_view=G.multi;
    server_event(G.active_server,0,why);
    application_event("DISCONNECTED","",G.username,why);
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
    server_event(G.active_server,m->rc,m->rc==RC_OK?"Connected":m->text[0]?m->text:rc_strerror(m->rc));
    if (m->rc != RC_OK) {
        G.connected=0; G.server_view=G.multi; G.chat.width=0; rooms_rebuild(); msgs_rebuild(1);
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
        }
        set_status_utf8(m->text[0] ? m->text : rc_strerror(m->rc));
        rooms_rebuild(); msgs_rebuild(0);
        return;
    }
    avatar_reset();
    G.connected = 1;
    ar_strlcpy(G.presence_user_id, m->user_id, sizeof(G.presence_user_id));
    ar_strlcpy(G.username, m->username, sizeof(G.username));
    application_event("CONNECTED","",G.username,"");
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
    prefs_save_client_state();
    login_close();
    cmd_simple(NC_PRESENCE);
    update_tz(m->server_now);

    rc_roomlist_free(&G.rooms);
    G.rooms = m->rooms;
    rc_roomlist_init(&m->rooms);
    notify_unread();
    rooms_rebuild();
    snprintf(s, sizeof(s), "Connected to %s as %s%s%s", G.host, m->username,
             m->server_version[0] ? " - Rocket.Chat " : "", m->server_version);
    set_status_utf8(s);

    /* Restore the conversation without marking it read in the server event view. */
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
    if (strcmp(m->rid, G.cur_rid) != 0 || m->a_interval != G.room_serial)
        return;   /* user already moved on */
    G.loading_rid[0] = 0;
    if (m->rc != RC_OK) {
        set_status_utf8(m->text[0] ? m->text : rc_strerror(m->rc));
        return;
    }
    G.history_exhausted = m->msgs.count < 50;
    rc_msglist_free(&G.msgs);
    rc_msglist_merge(&G.msgs, &m->msgs, MAX_MESSAGES);
    unread_resolve();
    ar_strlcpy(G.topic, m->topic, sizeof(G.topic));
    ar_free(G.members);
    G.members = m->members;
    m->members = NULL;
    update_tz(m->server_now);
    msgs_rebuild(1);
    if(G.unread_anchor[0] && !G.server_view) jump_unread();
    mark_dirty(DIRTY_HEADER);
    {
        char s[64];
        snprintf(s, sizeof(s), "%d messages", G.msgs.count);
        set_status(s);
    }
    {   /* opening marks the room read on the server; show that right away */
        int i;
        for (i = 0; i < G.rooms.count; i++)
            if (conversation_visible() && !strcmp(G.rooms.items[i].rid, G.cur_rid)) {
                G.rooms.items[i].unread = 0;
                G.rooms.items[i].alert = 0;
                G.rooms.items[i].mentions = 0;
            }
        rooms_rebuild();
    }
    if(conversation_visible()) { G.focus_visible=-1; sync_conversation_focus(); }
    application_event("ROOM",G.cur_rid,G.username,"");
}

static void on_sent(NetMsg *m)
{
    G.send_pending = 0;
    if (m->rc != RC_OK) {
        server_notice(G.active_server,m->text[0]?m->text:rc_strerror(m->rc));
        { char qualified[96]; snprintf(qualified,sizeof(qualified),"%d:%s",G.active_server+1,m->rid);
          application_event("SENDFAILED",qualified,G.username,m->text); }
        set_status("Delivery failed or unconfirmed; draft kept. Use Retry send.");send_state_refresh();
        return;
    }
    send_record_done(G.active_server,m);send_state_refresh();
    draft_sent(G.active_server,m->rid,m->a_text);
    { Buf current; buf_init(&current); composer_capture();
      latin1_to_utf8(&current, G.hook_buf);
      if (!G.composer_overlong && !strcmp(m->rid, G.cur_rid) && !strcmp(ar_trim(current.data ? current.data : (char *)""), m->a_text)) composer_set("", 0);
      buf_free(&current); hide_suggestions(); draft_checkpoint();
    }
    if (!strcmp(m->rid, G.cur_rid) && m->msgs.count) {
        remember_anchor();
        rc_msglist_merge(&G.msgs, &m->msgs, MAX_MESSAGES);
        msgs_rebuild(1);
    }
    if(m->rc==RC_OK) application_event("SENT",m->rid,G.username,m->a_text ? m->a_text : "");
}

static void on_update(NetMsg *m)
{
    dock_room_delta(G.active_server,&m->rooms);
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
            if (conversation_visible() && !strcmp(G.rooms.items[i].rid, G.cur_rid)) {
                G.rooms.items[i].unread = 0;
                G.rooms.items[i].alert = 0;
                G.rooms.items[i].mentions = 0;
            }
        rooms_rebuild();
    }
    { int j,k;
      for(j=0;j<m->msgs.count;j++) {
        for(k=0;k<G.msgs.count;k++) if(!strcmp(m->msgs.items[j].id,G.msgs.items[k].id)) break;
        if(k==G.msgs.count) { if(notification_allowed(G.active_server,m->rid,m->msgs.items[j].username,m->msgs.items[j].text)) application_event("MESSAGE",m->rid,m->msgs.items[j].username,m->msgs.items[j].text);
            if(G.multi) server_message_event(G.active_server,m->rid,m->msgs.items[j].username,m->msgs.items[j].text); }
      }
    }
    if ((m->msgs.count || m->msgs.n_deleted) && !strcmp(m->rid, G.cur_rid)) {
        remember_anchor();
        rc_msglist_merge(&G.msgs, &m->msgs, MAX_MESSAGES);
        msgs_rebuild(G.chat.follow);
    }
    unread_resolve();
    if(conversation_visible() && (m->msgs.count || m->rooms.count)) { G.focus_visible=-1; sync_conversation_focus(); }
    notify_unread();
    update_tz(m->server_now);
}

static void on_status(NetMsg *m)
{
    server_event(G.active_server,m->rc,m->text); G.servers[G.active_server].reconnecting=m->a_interval>0; health_status(G.active_server,m);
    if(m->rc==RC_OK) { ServerState *s=&G.servers[G.active_server]; s->last_error=s->cooling=s->health_retry=0; s->retry_until=0; }
    if (m->rc == RC_ERR_AUTH) {
        forget_login();
        went_offline("Logged out");
        server_event(G.active_server,m->rc,m->text); rooms_rebuild();
        return;
    }
    set_status_utf8(m->text); rooms_rebuild();
}

static void on_message_action(NetMsg *m);
static void handle_port(void)
{
    struct Message *msg;

    while ((msg = GetMsg(G.port)) != NULL) {
        NetMsg *m = (NetMsg *)msg;
        if(m->kind>=NC_THREAD && m->kind<=NC_THREAD_SEND && msg->mn_Node.ln_Type==NT_REPLYMSG) {
            G.pending--;
            if(m->kind==NC_REACT && m->a_interval==-1) {
                G.inline_reaction_pending=0;
                if(m->rc==RC_OK) {NetMsg *poll=netmsg_new(NC_POLL_NOW);if(poll)server_send(m->server_slot,poll);}
                else set_status_utf8(m->text[0]?m->text:"Reaction could not be confirmed");
            } else on_message_action(m);
            netmsg_free(m);continue;
        }
        if(m->kind==NC_SEARCH && msg->mn_Node.ln_Type==NT_REPLYMSG) { G.pending--; on_search(m); netmsg_free(m); continue; }
        if(m->kind==NE_NETWORK) {
            if(m->server_slot>=0 && m->server_slot<PREFS_SERVERS_MAX && m->server_generation==G.servers[m->server_slot].generation)
            { ServerState *s=&G.servers[m->server_slot];
                s->retry_until=amiga_local_ms()+(ar_ms)(m->a_before>m->a_interval?m->a_before:m->a_interval)*1000; s->cooling=m->a_interval>0; s->health_retry=m->a_verify || m->a_background;
                prefs_service_network(G.prefs_service,m->server_slot,m->text); mark_dirty(DIRTY_SIDE|DIRTY_HEADER);
            }
            worker_done(m); continue;
        }
        if(G.multi && (m->server_slot<0 || m->server_slot>=PREFS_SERVERS_MAX ||
            m->server_generation!=G.servers[m->server_slot].generation || m->server_slot!=G.active_server)) {
            if(m->server_slot>=0 && m->server_slot<PREFS_SERVERS_MAX && m->server_generation==G.servers[m->server_slot].generation) server_background(m);
            if(msg->mn_Node.ln_Type==NT_REPLYMSG) { G.pending--; netmsg_free(m); } else worker_done(m);
            rooms_rebuild(); continue;
        }
        if (msg->mn_Node.ln_Type == NT_REPLYMSG) {
            G.pending--;
            switch (m->kind) {
            case NC_CONNECT:   on_connected(m); break;
            case NC_OPEN_ROOM: on_room_opened(m); break;
            case NC_HISTORY: on_older(m); break;
            case NC_AVATAR: on_avatar(m); break;
            case NC_IMAGE: on_image(m); break;
            case NC_CREATE_ROOM:
                if (m->rc != RC_OK) request_utf8("New conversation", m->text[0] ? m->text : rc_strerror(m->rc));
                else {
                    rc_roomlist_merge(&G.rooms, &m->rooms); rooms_rebuild();
                    application_event("ROOMCREATED",m->rid,G.username,"");
                    ar_strlcpy(G.cur_rid, m->rid, sizeof(G.cur_rid));
                    rc_msglist_free(&G.msgs); G.topic[0] = 0; update_title(); msgs_rebuild(1); cmd_open_room(m->rid);
                }
                break;
            case NC_PRESENCE: on_presence(m); break;
            case NC_SEARCH: on_search(m); break;
            case NC_UPLOAD:
                G.download_pending=0; G.transfer_rc=m->rc;
                ar_strlcpy(G.transfer_status,m->text[0]?m->text:m->rc==RC_OK?"Attachment sent":rc_strerror(m->rc),sizeof(G.transfer_status));
                server_notice(G.active_server,G.transfer_status); set_status_utf8(G.transfer_status); if(m->rc==RC_OK) cmd_simple(NC_POLL_NOW); break;
            case NC_DOWNLOAD: on_downloaded(m); break;
            case NC_SEND:      on_sent(m); break;
            case NC_LOGOUT:    went_offline("Logged out"); break;
            case NC_DISCONNECT: went_offline("Disconnected"); break;
            }
            netmsg_free(m);
        } else {
            switch (m->kind) {
            case NE_TRANSFER:
                if(G.transfer_dialog && m->sent_at==G.transfer_serial && G.transfer_text && !G.transfer_cancelled) {
                    char *text=G.transfer_progress_text;
                    transfer_caption(m->a_before,m->a_interval,G.transfer_started,amiga_local_ms(),text,sizeof(G.transfer_progress_text));
                    SetGadgetAttrs((struct Gadget *)G.transfer_text,G.transfer_win,NULL,GA_Text,(ULONG)text,TAG_END);
                    RefreshGList((struct Gadget *)G.transfer_text,G.transfer_win,NULL,1);
                    if(G.transfer_gauge)SetGadgetAttrs((struct Gadget *)G.transfer_gauge,G.transfer_win,NULL,FUELGAUGE_Level,m->a_interval>0?(ULONG)(m->a_before*100/m->a_interval):0,TAG_END);
                }
                break;
            case NE_RECONNECTED: on_connected(m); break;
            case NE_UPDATE: on_update(m); break;
            case NE_PRESENCE: on_presence(m); break;
            case NE_NETWORK: prefs_service_network(G.prefs_service,m->server_slot,m->text); break;
            case NE_STATUS: on_status(m); break;
            }
            worker_done(m);
        }
    }
    notify_unread();
}

/* ---- main window --------------------------------------------------- */

static void about(void)
{
    snprintf(G.about, sizeof(G.about),
             "AmiRocket " AR_VERSION "\n"
             "A Rocket.Chat client for AmigaOS 3.2\n\n"
             "Networking: bsdsocket.library, TLS: AmiSSL\n"
             "Realtime events with polling fallback (%d seconds).", G.prefs.interval);
    request("About AmiRocket", G.about);
}

/* Native clipboard text: IFF FORM FTXT containing a Latin-1 CHRS chunk. */
static int clipboard_write(const char *text)
{
    struct MsgPort *port = CreateMsgPort();
    struct IOClipReq *io = port ? (struct IOClipReq *)CreateIORequest(port, sizeof(*io)) : NULL;
    Buf data;
    ULONG n = (ULONG)strlen(text), header[5];
    int ok = 0;
    buf_init(&data);
    header[0] = 0x464f524d; header[1] = 12 + n + (n & 1);
    header[2] = 0x46545854; header[3] = 0x43485253; header[4] = n;
    if (buf_append(&data, header, sizeof(header)) < 0 || buf_append(&data, text, n) < 0 ||
        ((n & 1) && buf_append(&data, "", 1) < 0)) goto done;
    if (io && !OpenDevice("clipboard.device", 0, (struct IORequest *)io, 0)) {
        io->io_Command = CMD_WRITE; io->io_Data = data.data; io->io_Length = data.len;
        if (!DoIO((struct IORequest *)io) && io->io_Actual == data.len) {
            io->io_Command = CMD_UPDATE; io->io_Data = NULL; io->io_Length = 0;
            ok = !DoIO((struct IORequest *)io);
        }
        CloseDevice((struct IORequest *)io);
    }
done:
    if (io) DeleteIORequest((struct IORequest *)io);
    if (port) DeleteMsgPort(port);
    buf_free(&data); return ok;
}

static void copy_message(void)
{
    int i;
    if (!strcmp(G.copy_rid, G.cur_rid)) for (i = 0; i < G.msgs.count; i++) {
        RcMsg *m = &G.msgs.items[i];
        if (!strcmp(m->id, G.copy_id)) {
            Buf plain, latin; int ok;
            buf_init(&plain); buf_init(&latin);
            md_to_plain(&plain, m->text ? m->text : "");
            utf8_to_latin1(&latin, buf_str(&plain), 1);
            ok = clipboard_write(buf_str(&latin));
            set_status(ok ? "Message copied" : "Could not write to clipboard");
            buf_free(&plain); buf_free(&latin); return;
        }
    }
    set_status("Click a message to select it first");
}

static void composer_clipboard(const char *command)
{
    G.copy_chat = 0; mark_dirty(DIRTY_CHAT);
    DoGadgetMethod((struct Gadget *)G.g[G_INPUT], G.win, NULL, GM_TEXTEDITOR_ARexxCmd, NULL, command);
    editor_wake();
    ActivateLayoutGadget((struct Gadget *)G.g[0], G.win, NULL, (ULONG)G.g[G_INPUT]);
}

static void apply_theme(int idx);
static void handle_preferences(void);
#include "ui_panels.inc"
#include "message_actions.inc"

static void launch_preferences_for_server(int slot)
{
    char path[512]; BPTR lock=0,dir=GetProgramDir(); Buf cmd;
    /* Keep portable/development copies usable; installed copies have no local
     * Preferences binary and use the recorded Prefs drawer instead. */
    if(dir && NameFromLock(dir,path,sizeof(path)) && AddPart(path,"AmiRocketPrefs",sizeof(path))) lock=Lock(path,ACCESS_READ);
    if(!lock) {
        ar_strlcpy(path,prefs_extra(&G.prefs,"workspace.prefs_dir","SYS:Prefs"),sizeof(path));
        if(AddPart(path,"AmiRocketPrefs",sizeof(path))) lock=Lock(path,ACCESS_READ);
    }
    if(!lock) { request("Preferences","Could not find AmiRocketPrefs. Reinstall it in SYS:Prefs or the configured Preferences drawer."); return; }
    UnLock(lock); buf_init(&cmd);
    if(!shell_argument(&cmd,path)) { buf_free(&cmd); return; }
    if(slot>=0) buf_printf(&cmd," EDIT=%d",slot+1);
    if (cmd.oom || launch_helper(buf_str(&cmd)) < 0)
        request("Preferences", "Could not start AmiRocketPrefs.");
    buf_free(&cmd);
}

static void launch_preferences(void) { launch_preferences_for_server(-1); }
#include "server_popup.inc"

static int preference_display_changed(const Prefs *p)
{
    return p->preview_width!=G.prefs.preview_width || p->low_memory!=G.prefs.low_memory || p->message_limit!=G.prefs.message_limit || p->image_cache_kib!=G.prefs.image_cache_kib || p->show_emoji != G.prefs.show_emoji || p->show_avatars != G.prefs.show_avatars ||
        p->auto_thumbnails != G.prefs.auto_thumbnails || p->inline_images != G.prefs.inline_images || p->ownscreen != G.prefs.ownscreen ||
        p->chat_font_size != G.prefs.chat_font_size || strcmp(p->theme, G.prefs.theme) || strcmp(p->chat_font, G.prefs.chat_font);
}
static void apply_preferences(const Prefs *next, int permanent)
{
    int display; NetMsg *m;
    if(next->extra_count<0 || next->extra_count>PREFS_EXTRA_MAX) return;
    display=preference_display_changed(next);
    if (G.running_client && (next->realtime != G.prefs.realtime || next->interval != G.prefs.interval || next->request_spacing_ms!=G.prefs.request_spacing_ms ||
        next->background_interval!=G.prefs.background_interval || next->avatar_disk_cache!=G.prefs.avatar_disk_cache || strcmp(next->cache_dir,G.prefs.cache_dir))) {
        m = netmsg_new(NC_SET_INTERVAL); if (m) { m->a_realtime=next->realtime; m->a_interval = next->interval; m->a_before=next->request_spacing_ms; m->a_background=next->background_interval; m->a_verify=next->avatar_disk_cache; m->a_text=ar_strdup(next->cache_dir); send_cmd(m); }
    }
    if(strcmp(G.prefs.cache_dir,next->cache_dir))images_reset();
    if(strcmp(G.prefs.notification_sound,next->notification_sound))notification_sound_clear();
    ar_strlcpy(G.prefs.notification_sound,next->notification_sound,sizeof(G.prefs.notification_sound));
    ar_strlcpy(G.prefs.cache_dir,next->cache_dir,sizeof(G.prefs.cache_dir));
    ar_strlcpy(G.prefs.download_dir,next->download_dir,sizeof(G.prefs.download_dir));
    G.prefs.preview_width=next->preview_width;
    G.prefs.low_memory=next->low_memory; G.prefs.message_limit=next->message_limit; G.prefs.image_cache_kib=next->image_cache_kib;
    if(display) { int i; RcMsgList empty; rc_msglist_init(&empty); rc_msglist_merge(&G.msgs,&empty,MAX_MESSAGES);
        for(i=0;i<PREFS_SERVERS_MAX;i++) rc_msglist_merge(&G.servers[i].msgs,&empty,MAX_MESSAGES);
        avatar_reset(); images_reset(); G.chat.width=0; msgs_rebuild(0); }
    G.prefs.request_spacing_ms=next->request_spacing_ms; G.prefs.background_interval=next->background_interval; G.prefs.avatar_disk_cache=next->avatar_disk_cache;
    G.prefs.realtime=next->realtime; G.prefs.interval = next->interval; G.prefs.show_emoji = next->show_emoji; G.prefs.show_avatars = next->show_avatars;
    G.prefs.inline_images = next->inline_images; G.prefs.ownscreen = next->ownscreen; G.prefs.open_links = next->open_links;
    G.prefs.auto_thumbnails=next->auto_thumbnails; G.prefs.external_images=next->external_images;
    G.prefs.preview_images = next->preview_images; G.prefs.chat_font_size = next->chat_font_size;
    ar_strlcpy(G.prefs.theme,next->theme,sizeof(G.prefs.theme)); ar_strlcpy(G.prefs.chat_font,next->chat_font,sizeof(G.prefs.chat_font));
    ar_strlcpy(G.prefs.browser,next->browser,sizeof(G.prefs.browser)); ar_strlcpy(G.prefs.image_viewer,next->image_viewer,sizeof(G.prefs.image_viewer));
    G.prefs.extra_count = next->extra_count; memcpy(G.prefs.extra,next->extra,sizeof(G.prefs.extra));
    if(G.multi) server_reconfigure(next);
    G.settings_override=!permanent; G.server_overrides=0;
    G.session_override = !permanent; G.plugins_dirty = 1;
    if (!G.prefs.show_avatars) avatar_reset();
    if (!G.prefs.inline_images) images_reset();
    if(display && (G.panel_depth || G.iconified)) G.defer_display=1;
    else if (display) { load_chat_font(); apply_theme(theme_find(G.prefs.theme)); }
    prefs_service_publish(G.prefs_service,&G.prefs);
    application_event("CONFIG",G.cur_rid,"",permanent ? "saved" : "session");
}
static void handle_preferences(void)
{
    PrefsUpdate *m;
    while ((m = prefs_service_next(G.prefs_service))) {
        if(m->server_only>0 && m->server_only<=PREFS_SERVERS_MAX) {
            int slot=m->server_only-1; Prefs *next=ar_malloc(sizeof(Prefs));
            if(next) {
                *next=G.prefs; next->servers[slot]=m->prefs.servers[slot]; next->servers_defined=1;
                server_reconfigure(next); ar_free(next);
                if(m->permanent) G.server_overrides&=~(1<<slot); else G.server_overrides|=1<<slot;
                G.session_override=G.settings_override || G.server_overrides;
                prefs_service_publish(G.prefs_service,&G.prefs);
            }
        } else apply_preferences(&m->prefs,m->permanent);
        prefs_update_free(m);
    }
}
static void reload_preferences(void)
{
    Prefs *next;
    if (!G.running_client || G.session_override) return;
    next = ar_malloc(sizeof(*next)); if (!next) return;
    if (!prefs_load(next) && (preference_display_changed(next) || next->realtime!=G.prefs.realtime || next->interval!=G.prefs.interval ||
        next->request_spacing_ms!=G.prefs.request_spacing_ms || next->background_interval!=G.prefs.background_interval ||
        next->avatar_disk_cache!=G.prefs.avatar_disk_cache || next->open_links!=G.prefs.open_links ||
        next->external_images!=G.prefs.external_images || next->preview_images!=G.prefs.preview_images || strcmp(next->browser,G.prefs.browser) || strcmp(next->image_viewer,G.prefs.image_viewer) ||
        memcmp(next->servers,G.prefs.servers,sizeof(next->servers)) || next->servers_defined!=G.prefs.servers_defined ||
        next->extra_count!=G.prefs.extra_count || memcmp(next->extra,G.prefs.extra,sizeof(next->extra)))) apply_preferences(next,1);
    memset(next,0,sizeof(*next)); ar_free(next);
}

#include "control.inc"

static void handle_menu(UWORD code)
{

    while (code != MENUNULL) {
        struct MenuItem *item = ItemAddress(G.menu, code);
        if (!item)
            break;
        switch ((ULONG)GTMENUITEM_USERDATA(item)) {
        case M_ICONIFY: if(!main_iconify()) request("Iconify","Could not create the Workbench icon."); break;
        case M_CONNECT:    if(G.multi && G.prefs.server[0]) server_connect(G.active_server,1); else login_open(NULL); break;
        case M_DISCONNECT: if(G.multi) server_disconnect(G.active_server); else if(G.connected) cmd_simple(NC_DISCONNECT); break;
        case M_LOGOUT:
            forget_login();
            if (G.connected)
                cmd_simple(NC_LOGOUT);
            break;
        case M_COMPLETE: if (G.n_sugg) complete_suggestion(G.sugg_sel); break;
        case M_NEXTSUGG: if (G.n_sugg) { G.sugg_sel = (G.sugg_sel + 1) % G.n_sugg; mark_dirty(DIRTY_CHAT); } break;
        case M_PREVSUGG: if (G.n_sugg) { G.sugg_sel = (G.sugg_sel + G.n_sugg - 1) % G.n_sugg; mark_dirty(DIRTY_CHAT); } break;
        case M_NEXTUNREAD: next_unread(); break;
        case M_JUMPUNREAD: seek_first_unread(); break;
        case M_OLDER: load_older(); break;
        case M_SEND: cmd_send_input(); break;
        case M_RETRYSEND: retry_failed_send();break;
        case M_FORGETSEND:forget_failed_send();break;
        case M_SWITCHNEXT:switch_conversation(1);break;
        case M_SWITCHPREV:switch_conversation(-1);break;
        case M_QUICKROOM:quick_conversation();break;
        case M_PREFS: launch_preferences(); break;
        case M_NEWDM: new_conversation(0); break;
        case M_NEWCHANNEL: new_conversation(1); break;
        case M_NEWPRIVATE: new_conversation(2); break;
        case M_SEARCH: search_window(); break;
        case M_UPLOAD: upload_attachment(); break;
        case M_CLEARCACHE: clear_media_caches(); break;
        case M_CLEARIMAGES: images_reset(); msgs_rebuild(0); set_status("Image preview cache cleared"); break;
        case M_SAVEFILE: {
            int i;
            for (i = 0; i < G.msgs.count; i++) if (!strcmp(G.copy_rid, G.cur_rid) && !strcmp(G.copy_id, G.msgs.items[i].id)) break;
            if (i < G.msgs.count && G.msgs.items[i].file_count) download_attachment(i, G.copy_file >= 0 && G.copy_file < G.msgs.items[i].file_count ? G.copy_file : 0);
            else set_status("Select a message with an attachment first");
            break;
        }
        case M_COPY: if (G.copy_chat) copy_message(); else composer_clipboard("COPY"); break;
        case M_CUT: composer_clipboard("CUT"); break;
        case M_PASTE: composer_clipboard("PASTE"); break;
        case M_OPENIMAGE: {
            int i; for(i=0;i<G.msgs.count;i++) if(!strcmp(G.copy_rid,G.cur_rid) && !strcmp(G.copy_id,G.msgs.items[i].id)) break;
            if(i<G.msgs.count) open_external_image(i,G.copy_file>=0 && G.copy_file<G.msgs.items[i].file_count?G.copy_file:0);
            break;
        }
        case M_CLEAREXTERNAL: external_images_clear(); break;
        case M_COPYMSG: copy_message(); break;
        case M_MESSAGE: message_actions(); break;
        case M_ABOUT:      about(); break;
        case M_QUIT:       G.quit = 1; break;
        case M_REFRESH:    if (G.connected) cmd_simple(NC_POLL_NOW); break;
        default:
            if ((ULONG)GTMENUITEM_USERDATA(item) >= M_PLUGIN_BASE) plugins_action((ULONG)GTMENUITEM_USERDATA(item)-M_PLUGIN_BASE);
            break;
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
        case WMHI_ICONIFY:
            if(!main_iconify()) request("Iconify","Could not create the Workbench icon.");
            break;
        case WMHI_UNICONIFY:
            if(!main_restore()) set_status("Cannot restore window; double-click the icon to retry");
            break;
        case WMHI_CLOSEWINDOW:
            G.quit = 1;
            break;
        case WMHI_MENUPICK:
            handle_menu(code);
            break;
        case WMHI_ACTIVE: reload_preferences(); break;
        case WMHI_NEWSIZE:
            G.chat_relayout = 1;
            mark_dirty(DIRTY_SIDE | DIRTY_CHAT | DIRTY_HEADER);
            break;
        case WMHI_RAWKEY:
#ifdef AR_GUI_QA
            if((result&WMHI_KEYMASK)==0x58) { G.quit=1; break; } /* F9 ends an isolated visual fixture. */
#endif
            if((result&WMHI_KEYMASK)==0x59 && G.multi && G.server_view) {
                struct IBox box;
                if(area_box(G.g[G_ROOMS],&box)) { G.side_menu_slot=G.active_server; G.side_menu_x=G.win->LeftEdge+box.Left+20; G.side_menu_y=G.win->TopEdge+box.Top+10; G.side_menu_pending=1; }
                break;
            }
            if((code&IEQUALIFIER_CONTROL) && editor_key(result&WMHI_KEYMASK,code)){input_events();break;}
            if((result&WMHI_KEYMASK)==0x45){G.unread_seek=0;hide_suggestions();if(G.g[G_INPUT])ActivateLayoutGadget((struct Gadget *)G.g[0],G.win,NULL,(ULONG)G.g[G_INPUT]);break;}
            /* NewMouse wheel codes, for mouse drivers without V47 wheel events */
            if ((result & WMHI_KEYMASK) == 0x7A)
                scroll_chat(G.chat.top - 3);
            else if ((result & WMHI_KEYMASK) == 0x7B)
                scroll_chat(G.chat.top + 3);
            break;
        case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
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
            case G_CONNECTB: if(G.multi && G.prefs.server[0]) server_connect(G.active_server,1); else login_open(NULL); break;
            case G_NEW: new_conversation(0); break;
            case G_PREFSB: launch_preferences(); break;
            case G_EMOJI: toolbar_format(":", "", ""); break;
            case G_BOLD: toolbar_format("*", "*", "text"); break;
            case G_ITALIC: toolbar_format("_", "_", "text"); break;
            case G_STRIKE: toolbar_format("~", "~", "text"); break;
            case G_INLINECODE: toolbar_format("`", "`", "code"); break;
            case G_CODEBLOCK: toolbar_format("```\n", "\n```", "code"); break;
            case G_UPLOAD: upload_attachment(); break;
            case G_SEARCH: search_window(); break;
            case G_FIRSTUNREAD: seek_first_unread(); break;
            case G_RETRYSEND: retry_failed_send(); break;
            case G_LINK: toolbar_format("[", "](https://)", "label"); break;
            case G_INPUT: editor_wake(); break;
            case G_FILTER: G.filter_changed=1;input_events();filtered_conversation();break;
            case G_SEND:
                cmd_send_input();
                break;
            }
            break;
        }
    }
    sync_conversation_focus();
    if(G.side_menu_pending) server_context_popup();
    if(G.action_menu_pending) { G.action_menu_pending=0; message_actions(); }
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
        { &IconBase, "icon.library", 39 },
        { &UtilityBase, "utility.library", 39 },
        { &WindowBase, "window.class", 44 },
        { &LayoutBase, "gadgets/layout.gadget", 44 },
        { &ButtonBase, "gadgets/button.gadget", 44 },
        { &FuelGaugeBase, "gadgets/fuelgauge.gadget", 44 },
        { &AslBase, "asl.library", 39 },
        { &DiskfontBase, "diskfont.library", 39 },
        { &TextFieldBase, "gadgets/texteditor.gadget", 47 },
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
    DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 44);
    G.editor_class = MakeClass(NULL, NULL, TEXTEDITOR_GetClass(), 0, 0);
    if (!G.editor_class) return -1;
    G.editor_class->cl_Dispatcher.h_Entry = (HOOKFUNC)HookEntry;
    G.editor_class->cl_Dispatcher.h_SubEntry = (HOOKFUNC)editor_dispatch;
    return 0;
}

static void close_libs(void)
{
    notification_sound_clear();
    if (G.editor_class) { FreeClass(G.editor_class); G.editor_class = NULL; }
    struct Library **all[] = {
        &FuelGaugeBase, &DataTypesBase, &TextFieldBase, &DiskfontBase, &AslBase, &ScrollerBase, &SpaceBase, &LabelBase, &CheckBoxBase, &ListBrowserBase, &StringBase, &ButtonBase, &LayoutBase,
        &IconBase, &WindowBase, &UtilityBase, &GadToolsBase, (struct Library **)&GfxBase,
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
    int n=plugins_count(),i,k=BASE_MENU_ITEMS;
    G.newmenu = ar_calloc(BASE_MENU_ITEMS+n+2,sizeof(struct NewMenu));
    if(!G.newmenu) return;
    memcpy(G.newmenu,g_newmenu,sizeof(g_newmenu));
    if(n) {
        G.newmenu[k].nm_Type=NM_TITLE; G.newmenu[k++].nm_Label=(STRPTR)"Plugins";
        for(i=0;i<n;i++,k++) { G.newmenu[k].nm_Type=NM_ITEM; G.newmenu[k].nm_Label=(STRPTR)plugins_label(i); G.newmenu[k].nm_UserData=(APTR)(ULONG)(M_PLUGIN_BASE+i); }
    }
    G.newmenu[k].nm_Type=NM_END;
    G.menu=CreateMenus(G.newmenu,GTMN_FrontPen,0,TAG_END);
    if(G.menu && G.vi) LayoutMenus(G.menu,G.vi,GTMN_NewLookMenus,TRUE,TAG_END);
}
static void refresh_plugins(void)
{
    if(!G.plugins_dirty && !(G.menu_rebuild && G.win_obj)) return;
    G.plugins_dirty=0;
    if(plugins_sync(&G.prefs)) G.menu_rebuild=1;
    if(G.menu_rebuild && G.win_obj) {
        if(G.win) ClearMenuStrip(G.win);
        SetAttrs(G.win_obj,WINDOW_MenuStrip,0,TAG_END);
        if(G.menu) FreeMenus(G.menu);
        ar_free(G.newmenu); G.menu=NULL; G.newmenu=NULL;
        build_menu(); SetAttrs(G.win_obj,WINDOW_MenuStrip,(ULONG)G.menu,TAG_END);
        G.menu_rebuild=0;
    }
    publish_plugins();
    if(plugins_error()[0]) set_status(plugins_error());
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
    G.pens.presence[0] = pen_rgb(0x20B887);
    G.pens.presence[1] = pen_rgb(0xD9A529);
    G.pens.presence[2] = pen_rgb(0xDE5454);
    int i;
    G.pens.emoji[0] = G.pens.bg;
    for (i = 1; i < EMOJI_COLOURS; i++)
        G.pens.emoji[i] = pen_rgb(emoji_palette[i]);
}

/* Guard custom drawing against foreground/background collisions in the
 * actual screen palette, including reduced-colour Workbenches. */
static LONG pen_lightness(LONG pen)
{
    ULONG rgb[3];
    GetRGB32(G.scr->ViewPort.ColorMap, (ULONG)pen, 1, rgb);
    return (30 * (LONG)(rgb[0] >> 24) + 59 * (LONG)(rgb[1] >> 24) +
            11 * (LONG)(rgb[2] >> 24)) / 100;
}

static LONG readable_pen(LONG bg, LONG preferred)
{
    LONG light = pen_lightness(bg), best = preferred;
    LONG distance = labs(pen_lightness(preferred) - light);
    struct DrawInfo *dri;
    int i;
    if (distance >= 64)
        return preferred;
    dri = GetScreenDrawInfo(G.scr);
    if (!dri)
        return preferred;
    for (i = 0; i < dri->dri_NumPens; i++) {
        LONG d = labs(pen_lightness(dri->dri_Pens[i]) - light);
        if (d > distance) {
            best = dri->dri_Pens[i];
            distance = d;
        }
    }
    FreeScreenDrawInfo(G.scr, dri);
    return best;
}

static void readable_view_pens(ViewPens *p)
{
    p->text = readable_pen(p->bg, p->text);
    p->dim = readable_pen(p->bg, p->dim);
    p->highlight = readable_pen(p->bg, p->highlight);
    p->accent_text = readable_pen(p->accent, p->accent_text);
    p->side_text = readable_pen(p->side_bg, p->side_text);
    p->side_dim = readable_pen(p->side_bg, p->side_dim);
    p->side_sel_text = readable_pen(p->side_sel, p->side_text);
    p->side_sel_dim = readable_pen(p->side_sel, p->side_dim);
    p->pill_text = readable_pen(p->pill_bg, p->text);
    { int i; for (i = 0; i < 6; i++) p->avatar_text[i] = readable_pen(p->avatar[i], p->text); }
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
        readable_view_pens(p);
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
    p->side_text = pen_rgb(t->rgb[TC_BARDETAIL]);
    p->side_dim = p->dim;
    p->sep = p->side_sel;
    p->pill_bg = p->side_sel;
    p->avatar[0] = p->accent;
    p->avatar[1] = pen_rgb(t->rgb[TC_BARTRIM]);
    p->avatar[2] = pen_rgb(t->rgb[TC_EXTRA3]);
    p->avatar[3] = pen_rgb(t->rgb[TC_EXTRA4]);
    p->avatar[4] = pen_rgb(t->rgb[TC_EXTRA5]);
    p->avatar[5] = pen_rgb(t->rgb[TC_EXTRA6]);
    readable_view_pens(p);
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

static int sidebar_weight(void)
{
    int n=atoi(prefs_extra(&G.prefs,"workspace.sidebar","30")); return n<15?15:n>65?65:n;
}
static int composer_rows(void)
{
    int n=atoi(prefs_extra(&G.prefs,"workspace.composer_rows","4"));
    return n<2?2:n>12?12:n;
}
static int composer_weight(void)
{
    int h=G.prefs.win_height>200?G.prefs.win_height:400;
    int n;
    h-=(G.scr->RastPort.TxHeight+2)*4+20; if(h<100) h=100;
    n=((G.scr->RastPort.TxHeight+2)*composer_rows()+6)*100/h;
    return n<5?5:n>45?45:n;
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
    G.filter_hook.h_Entry = (HOOKFUNC)HookEntry;
    G.filter_hook.h_SubEntry = (HOOKFUNC)filter_hook_func;
    memset(&G.editor_listener, 0, sizeof(G.editor_listener));
    G.editor_listener.onCharEntered = editor_char;
    G.editor_listener.onCharsInserted = editor_change;
    G.editor_listener.onCharsDeleted = editor_change;

    G.g[G_STATUS]=G.g[G_COMPOSELABEL]=NULL;
    root = G.g[0] = VLayoutObject,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_InnerSpacing, 0,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_InnerSpacing, 0,
            LAYOUT_AddChild, VLayoutObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_AddChild,panel_button(G_NEW,"Create New"), CHILD_WeightedHeight,0,
                LAYOUT_AddChild, ButtonObject,
                    GA_ReadOnly, TRUE, GA_Text, (ULONG)"Find rooms / people",
                    BUTTON_Justification, BCJ_LEFT,
                End,
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, G.g[G_FILTER] = StringObject,
                    GA_ID, G_FILTER, GA_RelVerify, TRUE,
                    STRINGA_MaxChars, sizeof(G.filter) - 1,
                    STRINGA_TextVal, (ULONG)G.filter,
                    STRINGA_EditHook, (ULONG)&G.filter_hook,
                End,
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, G.g[G_ROOMS] = SpaceObject,
                GA_ID, G_ROOMS,
                GA_ReadOnly, TRUE,
                GA_Immediate, TRUE,
                GA_RelVerify, TRUE,
                ICA_TARGET, ICTARGET_IDCMP,
                SPACE_Transparent, TRUE,
                SPACE_RenderHook, (ULONG)&G.render_hook,
            End,
            LAYOUT_AddChild, G.g[G_SIDESCROLL] = ScrollerObject,
                GA_ID, G_SIDESCROLL,
                GA_RelVerify, TRUE,
                SCROLLER_Orientation, SORIENT_VERT,
                SCROLLER_Arrows, TRUE,
                ICA_TARGET, ICTARGET_IDCMP,
            End,
            CHILD_WeightedWidth, 0,
                End,
            End,
            CHILD_WeightedWidth, sidebar_weight(),
            CHILD_WeightBar, TRUE,
            CHILD_MinWidth, minside,
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
                        GA_ReadOnly, TRUE,
                        GA_Immediate, TRUE,
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
                CHILD_WeightedHeight,100-composer_weight(),
                CHILD_WeightBar,TRUE,
                LAYOUT_AddChild, VLayoutObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_InnerSpacing, 0,
                    LAYOUT_VertSpacing, 0,
                    LAYOUT_EvenSize, FALSE,
                    LAYOUT_AddChild, HLayoutObject,
                    LAYOUT_InnerSpacing, 0,
                    LAYOUT_AddChild, G.g[G_INPUT] = NewObject(G.editor_class, NULL,
                        GA_ID, G_INPUT, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
                        GA_TEXTEDITOR_FixedFont, FALSE,
                        GA_TEXTEDITOR_ExportWrap, 0,
                        GA_TEXTEDITOR_LineEndingExport, LINEENDING_LF,
                        GA_TEXTEDITOR_Contents, (ULONG)(G.reopen_draft ? G.reopen_draft : (STRPTR)G.hook_buf),
                        ICA_TARGET, ICTARGET_IDCMP,
                        TAG_END),
                    CHILD_MinHeight, (G.scr->RastPort.TxHeight + 2) * 2 + 6,
                    End,
                    CHILD_MinHeight, (G.scr->RastPort.TxHeight + 2) * 2 + 6,
                    CHILD_WeightedHeight, 100,
                    LAYOUT_AddChild,HLayoutObject,
                        LAYOUT_AddChild,panel_button(G_FIRSTUNREAD,"First unread"),CHILD_WeightedWidth,0,
                        LAYOUT_AddChild,G.g[G_RETRYSEND]=panel_button(G_RETRYSEND,"Retry unconfirmed send..."),CHILD_WeightedWidth,0,
                        LAYOUT_AddChild,SpaceObject,End,
                    End,CHILD_WeightedHeight,0,
                    LAYOUT_AddChild, HLayoutObject, LAYOUT_InnerSpacing, 1,
                        LAYOUT_AddChild, panel_button(G_EMOJI, ":)"), CHILD_WeightedWidth, 0,
                        LAYOUT_AddChild, panel_button(G_BOLD, "B"), CHILD_WeightedWidth, 0,
                        LAYOUT_AddChild, panel_button(G_ITALIC, "I"), CHILD_WeightedWidth, 0,
                        LAYOUT_AddChild, panel_button(G_STRIKE, "S"), CHILD_WeightedWidth, 0,
                        LAYOUT_AddChild, panel_button(G_INLINECODE, "<>"), CHILD_WeightedWidth, 0,
                        LAYOUT_AddChild, panel_button(G_CODEBLOCK, "Code"), CHILD_WeightedWidth, 0,
                        LAYOUT_AddChild, panel_button(G_LINK, "Link"), CHILD_WeightedWidth, 0,
                        LAYOUT_AddChild, panel_button(G_UPLOAD, "File"), CHILD_WeightedWidth,0,
                        LAYOUT_AddChild, panel_button(G_SEARCH, "Find"), CHILD_WeightedWidth,0,

                        LAYOUT_AddChild, SpaceObject, End,
                        LAYOUT_AddChild, G.g[G_SEND] = panel_button(G_SEND, "_Send"), CHILD_WeightedWidth, 0,
                    End, CHILD_WeightedHeight, 0,
                End,
                CHILD_WeightedHeight, composer_weight(),
            End,
            CHILD_WeightedWidth, 100-sidebar_weight(),
        End,
    End;
    if (!root)
        return -1;
    G.app_port=CreateMsgPort();
    if(!G.app_port) { DisposeObject(root); return -1; }
    G.app_icon=GetDiskObject("PROGDIR:AmiRocket");
    if(!G.app_icon) G.app_icon=GetDefDiskObject(WBTOOL);
    G.win_obj = WindowObject,
        WINDOW_AppPort, (ULONG)G.app_port,
        WINDOW_IconifyGadget, TRUE,
        WINDOW_IconTitle, (ULONG)"AmiRocket",
        G.app_icon ? WINDOW_Icon : TAG_IGNORE, (ULONG)G.app_icon,
        WA_Title, (ULONG)G.title,
        WA_ScreenTitle, (ULONG)"AmiRocket " AR_VERSION,
        WA_PubScreen, (ULONG)G.scr,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_Activate, TRUE, WA_ReportMouse,TRUE,
        WA_Width, G.prefs.win_width,
        WA_Height, G.prefs.win_height,
        G.prefs.win_left >= 0 ? WA_Left : TAG_IGNORE, G.prefs.win_left,
        G.prefs.win_top >= 0 ? WA_Top : TAG_IGNORE, G.prefs.win_top,
        G.prefs.win_left < 0 ? WINDOW_Position : TAG_IGNORE, WPOS_CENTERSCREEN,
        G.menu ? WINDOW_MenuStrip : TAG_IGNORE, (ULONG)G.menu,
        WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_GADGETDOWN | IDCMP_GADGETUP | IDCMP_CLOSEWINDOW | IDCMP_MENUPICK |
                  IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW | IDCMP_RAWKEY | IDCMP_VANILLAKEY |
                  IDCMP_MOUSEMOVE | IDCMP_IDCMPUPDATE | IDCMP_EXTENDEDMOUSE | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW,
        WINDOW_IDCMPHook, (ULONG)&G.idcmp_hook,
        WINDOW_IDCMPHookBits, IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS | IDCMP_GADGETDOWN | IDCMP_IDCMPUPDATE | IDCMP_EXTENDEDMOUSE,
        WINDOW_ParentGroup, (ULONG)root,
    End;
    if (!G.win_obj) {
        DisposeObject(root);
        if(G.app_icon) FreeDiskObject(G.app_icon);
        G.app_icon=NULL;
        DeleteMsgPort(G.app_port); G.app_port=NULL;
        return -1;
    }
    G.win = RA_OpenWindow(G.win_obj);
    if (G.win) {
        DoMethod(G.g[G_INPUT], GM_TEXTEDITOR_AddChangeListener, &G.editor_listener);
        if (G.reopen_draft) { FreeVec(G.reopen_draft); G.reopen_draft = NULL; }
    }
    return G.win ? 0 : -1;
}

static void remember_window(void)
{
    if (!G.win)
        return;
    { struct IBox box; char value[16];
      if(area_box(G.g[G_ROOMS],&box) && G.win->Width>0) {
          snprintf(value,sizeof(value),"%d",(box.Width+18)*100/G.win->Width); prefs_set(&G.prefs,"workspace.sidebar",value);
      }
    }
    { ULONG height=0; char value[16];
      if(GetAttr(GA_Height,G.g[G_INPUT],&height) && height>6) {
        int n=(height-6)/(G.scr->RastPort.TxHeight+2); if(n<2) n=2; if(n>12) n=12;
        snprintf(value,sizeof(value),"%d",n); prefs_set(&G.prefs,"workspace.composer_rows",value);
      }
    }
    G.prefs.win_left = G.win->LeftEdge;
    G.prefs.win_top = G.win->TopEdge;
    G.prefs.win_width = G.win->Width;
    G.prefs.win_height = G.win->Height;
}

/* window.class keeps the gadget tree alive while its Intuition window is closed. */
static int main_iconify(void)
{
    if(G.iconified) return 1;
    if(!G.win_obj || !G.win || G.panel_depth || G.lwin_obj) return 0;
    composer_capture(); remember_anchor(); remember_window();
    if(!RA_Iconify(G.win_obj)) return 0;
    G.win=NULL; G.iconified=1; G.side_menu_pending=0;
    sync_conversation_focus();
    if(G.own_screen) ScreenToBack(G.scr);
    application_event("WINDOW",G.cur_rid,"","iconified");
    return 1;
}
static int main_restore(void)
{
    if(!G.iconified) return G.win!=NULL;
    G.win=RA_OpenWindow(G.win_obj);
    if(!G.win) return 0;
    G.iconified=0;
    if(G.scr) ScreenToFront(G.scr);
    WindowToFront(G.win); ActivateWindow(G.win);
    G.chat_relayout=1; update_title(); set_busy(G.busy);
    refresh_plugins(); mark_dirty(DIRTY_SIDE|DIRTY_CHAT|DIRTY_HEADER); redraw_dirty();
    sync_conversation_focus();
    application_event("WINDOW",G.cur_rid,"","restored");
    return 1;
}

static void close_main(void)
{
    images_drop_pictures();
    avatar_drop_pictures();
    if (G.win_obj)
        DisposeObject(G.win_obj);
    G.win_obj = NULL;
    G.win = NULL; G.iconified=0;
    if(G.app_icon) FreeDiskObject(G.app_icon);
    G.app_icon=NULL;
    if(G.app_port) { struct Message *m; while((m=GetMsg(G.app_port))) ReplyMsg(m); DeleteMsgPort(G.app_port); G.app_port=NULL; }
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
    if (G.win) {
        G.reopen_draft = (STRPTR)DoGadgetMethod((struct Gadget *)G.g[G_INPUT], G.win, NULL, GM_TEXTEDITOR_ExportText, NULL);
        if (!G.reopen_draft) { request("AmiRocket", "Not enough memory to preserve the draft; theme unchanged."); return; }
    }
    composer_capture();

    ar_strlcpy(G.prefs.theme, theme_get(idx)->id, sizeof(G.prefs.theme));
    remember_window();
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
    servers_init();
    load_chat_font();
    update_tz(0);
    G.port = CreateMsgPort();
    if (!G.port || worker_start(G.port, err, sizeof(err)) < 0) {
        request("AmiRocket", G.port ? err : "Out of memory");
        rc = RETURN_FAIL;
        goto out;
    }
    G.running_client = 1; draft_start();
    G.prefs_service = prefs_service_start(&G.prefs);
    plugins_start_host();
    arexx_start(control_command);
    {
        NetMsg *m = netmsg_new(NC_SET_INTERVAL);
        if (m) {
            m->a_realtime=G.prefs.realtime; m->a_interval = G.prefs.interval; m->a_before=G.prefs.request_spacing_ms;
            m->a_background=G.prefs.background_interval; m->a_verify=G.prefs.avatar_disk_cache; m->a_text=ar_strdup(G.prefs.cache_dir);
            send_cmd(m);
        }
    }
    if (open_main() < 0) {
        request("AmiRocket", "Cannot open the main window.");
        rc = RETURN_FAIL;
        goto out;
    }

    G.focus_slot=-1; sync_conversation_focus();
    { int i,defined=0;
      for(i=0;i<PREFS_SERVERS_MAX;i++) if(G.prefs.servers[i].url[0]) {
          defined++; if(G.prefs.servers[i].autoconnect) server_connect(i,i==G.active_server);
      }
      rooms_rebuild(); if(!defined) login_open(NULL);
    }

    while (!G.quit) {
        ULONG wsig = 0, lsig = 0, sigs;
        if (G.win_obj)
            GetAttr(WINDOW_SigMask, G.win_obj, &wsig);
        if (G.lwin_obj)
            GetAttr(WINDOW_SigMask, G.lwin_obj, &lsig);
        input_events();
        if(G.defer_display && !G.panel_depth && !G.iconified) { G.defer_display=0; load_chat_font(); apply_theme(theme_find(G.prefs.theme)); }
        refresh_plugins();
        if (G.dirty)
            redraw_dirty();
        if(G.app_port) wsig|=1UL<<G.app_port->mp_SigBit;
        sigs = Wait(wsig | lsig | client_tick_signal() | arexx_signal() | prefs_service_signal(G.prefs_service) | (1UL << G.port->mp_SigBit) | SIGBREAKF_CTRL_C |
                    (G.dirty_sig >= 0 ? 1UL << G.dirty_sig : 0));
        if(sigs & client_tick_signal()) client_tick_handle();
        if (sigs & arexx_signal()) arexx_handle();
        if (sigs & prefs_service_signal(G.prefs_service)) handle_preferences();
        if (sigs & SIGBREAKF_CTRL_C)
            G.quit = 1;
        if (sigs & (1UL << G.port->mp_SigBit))
            handle_port();
        if (wsig && (sigs & wsig))
            handle_main();
        if (G.lwin_obj && (sigs & lsig))
            handle_login();
    }
    draft_checkpoint();
    remember_window();
    prefs_save_client_state();

out:
    draft_stop();
    plugins_stop(); arexx_stop();
    prefs_service_stop(G.prefs_service); G.prefs_service = NULL;
    worker_stop();
    servers_cleanup();
    images_reset();
    { int i; for(i=0;i<4;i++) external_image_forget(i,0); }
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
    if (G.reopen_draft) { FreeVec(G.reopen_draft); G.reopen_draft = NULL; }
    if (G.chat_font) { CloseFont(G.chat_font); G.chat_font = NULL; }
    if (G.code_font) { CloseFont(G.code_font); G.code_font = NULL; }
    avatar_reset();
    close_libs();
    return rc;
}

/* Isolated AmigaOS GUI checks: no account, server, or saved preferences. */
#define AR_GUI_QA 1
#include "../src/amiga/gui.c"
#include "../tests/test_core.h"
#include "../sdk/include/amirocket_rexx.h"
#include <dos/dostags.h>
#include <rexx/storage.h>
#include <proto/rexxsyslib.h>
static void qa_trace(const char *s) { Write(Output(), (APTR)s, (LONG)strlen(s)); }
static void qa_log(const char *s) { qa_trace(s); }
unsigned long __stack = 65536;
static int qa_fail, qa_pass;
extern int worker_recovery_qa(int *passed);
#define QA(x) do { if (x) qa_pass++; else { qa_fail++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
#include "extensions_qa.inc"
static void qa_message(RcMsg *m,int n);
#include "servers_qa.inc"
#include "dock_qa.inc"
#include "unread_qa.inc"

static void qa_key(UWORD key)
{
    struct InputEvent event = {0}; struct gpInput input = {0};
    event.ie_Class = IECLASS_RAWKEY; event.ie_Code = key;
    input.MethodID = GM_HANDLEINPUT; input.gpi_IEvent = &event;
    QA(DoMethodA(G.g[G_INPUT], (Msg)&input) == GMR_MEACTIVE);
    input_events();
}

static ULONG qa_panel_hash(PrefsPanel *p)
{
    struct Gadget *g = (struct Gadget *)p->page;
    ULONG hash = 0; int x, y;
    for (y = g->TopEdge; y < g->TopEdge + g->Height; y += 7)
        for (x = g->LeftEdge; x < g->LeftEdge + g->Width; x += 11)
            hash = hash * 33 + ReadPixel(p->win->RPort, x, y);
    return hash;
}

static void qa_message(RcMsg *m, int n)
{
    char id[32];
    snprintf(id, sizeof(id), "qa-%d", n);
    m->id = ar_strdup(id); m->rid = ar_strdup("r1");
    m->username = ar_strdup(n % 2 ? "alice" : "bob");
    m->text = ar_strdup("A readable chat message with wrapping, accents: caf\xC3\xA9.\nSecond line.");
    m->ts_ms = iso8601_to_ms("2026-01-01T00:00:00Z") + (ar_ms)n * 60000;
    m->updated_ms = m->ts_ms;
}
static int qa_run(int argc, char **argv)
{
    int i, row, file, message;
    char anchor[64];
    struct TextFont *ui_font;
    NetMsg *older;
    qa_trace("QA start\n");
    if (ar_mem_init() < 0) return 20;
    QA(run_core_tests(qa_log) == 0);
    memset(&G, 0, sizeof(G)); G.dirty_sig = -1;
    qa_trace("QA open libraries\n");
    if (open_libs() < 0) { close_libs(); ar_mem_cleanup(); return 20; }
    qa_trace("QA libraries open\n");
    prefs_defaults(&G.prefs); G.prefs.win_width = 640; G.prefs.win_height = 400;
    load_chat_font();
    side_init(&G.side); chat_init(&G.chat); rc_roomlist_init(&G.rooms); rc_msglist_init(&G.msgs);
    G.port = CreateMsgPort();
    G.rooms.items = ar_calloc(4, sizeof(RcRoom)); G.rooms.count = G.rooms.cap = 4;
    for (i = 0; i < 4; i++) {
        char id[8]; snprintf(id, sizeof(id), "r%d", i + 1);
        G.rooms.items[i].rid = ar_strdup(id); G.rooms.items[i].open = 1;
        G.rooms.items[i].name = ar_strdup(i == 0 ? "general" : i == 1 ? "alice,bob,you" : i == 2 ? "alice,charlie,you" : "alice");
        G.rooms.items[i].type = i ? 'd' : 'c';
    }
    strcpy(G.cur_rid, "r1"); strcpy(G.username, "you"); G.room_serial = 7;
    G.msgs.items = ar_calloc(60, sizeof(RcMsg)); G.msgs.count = G.msgs.cap = 60;
    for (i = 0; i < 60; i++) qa_message(&G.msgs.items[i], i + 100);
    G.msgs.items[40].files = ar_calloc(1, sizeof(RcAttachment));
    G.msgs.items[40].file_count = 1;
    G.msgs.items[40].files[0].title = ar_strdup("photo.png");
    G.msgs.items[40].files[0].url = ar_strdup("/file-upload/x/photo.png");
    G.msgs.items[40].files[0].image = 1;
    qa_trace("QA open main\n");
    if (open_main() < 0) { printf("FAIL open_main\n"); qa_fail++; goto done; }
    qa_trace("QA main open\n");
    handle_main(); rooms_rebuild(); msgs_rebuild(1); update_title(); redraw_dirty();
    QA(G.g[G_FILTER] && G.g[G_INPUT] && !G.g[G_COMPOSELABEL] && !G.g[G_STATUS]);
    QA(G.win->Width <= G.scr->Width && G.win->Height <= G.scr->Height);
    { struct IBox b; QA(area_box(G.g[G_ROOMS], &b) && b.Width > 50); }
    { ULONG x = 0; GetAttr(GA_Height, G.g[G_INPUT], &x); QA(x >= 2*G.scr->RastPort.TxHeight && x <= (ULONG)(2*G.scr->RastPort.TxHeight+16)); }
    composer_set("abc\nxyz", 5); composer_capture();
    QA(!strncmp(G.hook_buf, "abc\nxyz", 7)); QA(G.hook_pos == 5);
    { NetMsg *sent = netmsg_new(NC_SEND);
      sent->a_text = ar_strdup("abc\nxyz"); strcpy(sent->rid, "other-room");
      on_sent(sent); composer_capture(); QA(!strncmp(G.hook_buf, "abc\nxyz", 7));
      strcpy(sent->rid, "r1"); composer_set("a newer draft", 5);
      on_sent(sent); composer_capture(); QA(!strncmp(G.hook_buf, "a newer draft", 13));
      composer_set("abc\nxyz", 5); on_sent(sent); composer_capture();
      QA(!G.hook_buf[0] || !strcmp(G.hook_buf, "\n")); netmsg_free(sent);
    }
    composer_set("@al", 3); composer_capture(); QA(G.hook_pos == 3);
    handle_main(); G.input_changed = 0;
    DoGadgetMethod((struct Gadget *)G.g[G_INPUT], G.win, NULL, GM_TEXTEDITOR_InsertText, NULL, "q", GV_TEXTEDITOR_InsertText_Cursor);
    QA(G.input_changed);
    composer_capture(); QA(!strncmp(G.hook_buf, "@alq", 4));
    qa_trace("QA popup keys\n");
    /* Feed popup keys through the real editor subclass dispatcher. */
    composer_set("@al", 3); composer_capture();
    G.n_sugg = 2; G.sugg_sel = 0; G.sugg_at = 0; G.sugg_end = 3;
    strcpy(G.sugg[0].user, "alice"); strcpy(G.sugg[1].user, "bob");
    qa_key(0x4D); QA(G.sugg_sel == 1 && G.hook_pos == 3);
    qa_key(0x4C); QA(G.sugg_sel == 0 && G.hook_pos == 3);
    qa_key(0x4C); QA(G.sugg_sel == 1); /* wrap backwards */
    qa_key(0x42); composer_capture(); QA(!strncmp(G.hook_buf, "@bob ", 5) && G.n_sugg == 0);
    composer_set("@al", 3); composer_capture(); G.n_sugg = 2; G.sugg_sel = 0; G.sugg_at = 0; G.sugg_end = 3;
    qa_key(0x44); composer_capture(); QA(!strncmp(G.hook_buf, "@alice ", 7));
    G.n_sugg = 2; qa_key(0x45); QA(G.n_sugg == 0);
    qa_trace("QA clipboard and Enter keys\n");
    G.n_sugg = 0; G.editor_action = EDITOR_NONE;
    QA(editor_key(0x44, 0) && G.editor_action == EDITOR_SEND);
    G.editor_action = EDITOR_NONE;
    QA(editor_key(0x43, 0) && G.editor_action == EDITOR_SEND);
    G.editor_action = EDITOR_NONE;
    QA(editor_key(0x44, IEQUALIFIER_LSHIFT) && G.editor_action == EDITOR_NEWLINE);
    QA(editor_key(0x44, IEQUALIFIER_RSHIFT) && G.editor_action == EDITOR_NEWLINE);
    G.n_sugg = 2;
    QA(editor_key(0x44, IEQUALIFIER_LSHIFT) && G.editor_action == EDITOR_NEWLINE);
    G.n_sugg = 0;
    composer_set("firstsecond", 5); input_events(); composer_capture();
    QA(!strncmp(G.hook_buf, "first\nsecond", 12));
    composer_set("Copy caf\xe9\nsecond line", 0);
    DoGadgetMethod((struct Gadget *)G.g[G_INPUT], G.win, NULL, GM_TEXTEDITOR_MarkText, NULL, 0, 0, 11, 1);
    composer_clipboard("COPY");
    composer_set("", 0); composer_clipboard("PASTE"); composer_capture();
    QA(!strncmp(G.hook_buf, "Copy caf\xe9\nsecond line", 21));
    DoGadgetMethod((struct Gadget *)G.g[G_INPUT], G.win, NULL, GM_TEXTEDITOR_MarkText, NULL, 0, 0, 11, 1);
    composer_clipboard("CUT"); composer_capture();
    QA(!G.hook_buf[0] || !strcmp(G.hook_buf, "\n"));
    strcpy(G.copy_id, G.msgs.items[0].id); strcpy(G.copy_rid, "r1"); G.copy_chat = 1;
    copy_message();
    composer_set("", 0); composer_clipboard("PASTE"); composer_capture();
    QA(strstr(G.hook_buf, "readable chat message") && strstr(G.hook_buf, "Second line."));
    qa_trace("QA code blocks and links\n");
    { ChatView cv; RcMsg m = {0}; RcMsgList messages = {0}; struct RastPort rp = *G.win->RPort;
      int code = 0, indented = 0, literal = 0, j;
      m.id = "rich"; m.username = "alice"; m.ts_ms = 1760000000000LL;
      m.text = "Before https://example.com/a/very/long/path?keep=1&other=2 and https://two.example/ok.\n```yaml\n  alias: *literal* :smile:\n\tkey: value\n\n```\nAfter [docs](https://docs.example/page).";
      messages.items = &m; messages.count = 1;
      chat_init(&cv); cv.code_font = G.code_font;
      if (G.chat_font) SetFont(&rp, G.chat_font);
      chat_layout(&cv, &rp, 240, &messages, 0, 0, "you", ""); cv.visible = 100;
      QA(G.code_font && !(G.code_font->tf_Flags & FPF_PROPORTIONAL));
      for (j = 0; j < cv.n; j++) if (cv.v[j].kind == VL_CODE) {
        const VLine *line = &cv.v[j]; const char *text = cv.pool.data + line->off;
        code++;
        if (line->len >= 2 && text[0] == ' ' && text[1] == ' ') indented++;
        if (line->len >= 17 && !strncmp(text, "  alias: *literal*", 17)) literal++;
      }
      QA(code >= 4 && indented >= 2 && literal == 1);
      QA(cv.nlinks >= 3);
      for (j = 0; j < cv.nlinks; j++) {
        const VLink *link = &cv.links[j]; const char *url;
        cv.top = link->row;
        url = chat_link_hit(&cv, link->x + 1, 1);
        QA(url && !strcmp(url, cv.urls.data + link->url));
        QA(!chat_link_hit(&cv, link->x - 1, 1) || j > 0);
      }
      cv.top = 0;
      QA(strstr(cv.urls.data, "keep=1&other=2") != NULL);
      chat_free(&cv);
    }
    { Buf cmd; buf_init(&cmd);
      QA(web_link_command(&cmd, "https://example.com/a?x=1&y=2"));
      QA(!strcmp(buf_str(&cmd), "\"C:OpenURL\" \"https://example.com/a?x=1&y=2\""));
      buf_clear(&cmd); QA(!web_link_command(&cmd, "file:///SYS/S/startup-sequence"));
      QA(!web_link_command(&cmd, "https://example.com/\nquit"));
      buf_clear(&cmd); QA(web_link_command(&cmd, "https://example.com/a*\"b"));
      QA(strstr(buf_str(&cmd), "a***\"b") != NULL); buf_free(&cmd);
    }
    qa_trace("QA toolbar and preferences sections\n");
    composer_set("hello world", 0);
    DoGadgetMethod((struct Gadget *)G.g[G_INPUT], G.win, NULL, GM_TEXTEDITOR_MarkText, NULL, 0, 0, 5, 0);
    toolbar_format("*", "*", "text"); composer_capture(); QA(!strncmp(G.hook_buf, "*hello* world", 13));
    composer_set("", 0); toolbar_format("```\n", "\n```", "code"); composer_capture();
    QA(!strncmp(G.hook_buf, "```\ncode\n```", 12));
    { PrefsPanel panel; Prefs next; ULONG current = 99, previous; int section;
      QA(prefs_panel_build(&panel));
      if (panel.obj && panel.win) {
        QA(panel.win->Width <= G.scr->Width && panel.win->Height <= G.scr->Height);
        previous = qa_panel_hash(&panel);
        for (section = 0; section < 7; section++) {
          prefs_panel_select(&panel, section);
          GetAttr(PAGE_Current, panel.page, &current); QA(current == (ULONG)section);
          if (section) QA(qa_panel_hash(&panel) != previous);
          previous = qa_panel_hash(&panel);
        }
        QA(panel.catalog && panel.catalog->count==2);
        if(panel.catalog && panel.catalog->count) {
            PluginEntry *e=&panel.catalog->entry[0]; char key[80];
            QA(!strcmp(e->version,"1.0"));
            snprintf(key,sizeof(key),"plugin.%s.path",e->id); prefs_set(panel.plugin_draft,key,e->path);
            snprintf(key,sizeof(key),"plugin.%s.enabled",e->id); prefs_set(panel.plugin_draft,key,"1");
            prefs_plugin_rows(&panel);
            QA(prefs_panel_read(&panel,&next) && !strcmp(prefs_extra(&next,key,""),"1"));
        }
        if(argc>1 && !strcmp(argv[1],"PREFS")) { prefs_panel_select(&panel,2); qa_trace("QA visual plugin review\n"); Delay(1500); }
        prefs_panel_select(&panel, 0);
        qa_trace("QA preferences read and validate\n");
        QA(prefs_panel_read(&panel, &next) && next.show_emoji && next.show_avatars && next.inline_images);
        SetGadgetAttrs((struct Gadget *)panel.field[P_INTERVAL], panel.win, NULL, STRINGA_TextVal, (ULONG)"0", TAG_END);
        QA(!prefs_panel_read(&panel, &next));
        SetGadgetAttrs((struct Gadget *)panel.field[P_INTERVAL], panel.win, NULL, STRINGA_TextVal, (ULONG)"30", TAG_END);
        SetGadgetAttrs((struct Gadget *)panel.field[P_AVATARS], panel.win, NULL, GA_Selected, FALSE, TAG_END);
        QA(prefs_panel_read(&panel, &next) && next.interval == 30 && !next.show_avatars);
        prefs_panel_dispose(&panel);
      }
    }
    qa_trace("QA presence and avatars\n");
    /* Cosmetic metadata is allowed to be absent. */
    { NetMsg *presence = netmsg_new(NC_PRESENCE);
      presence->members = ar_strdup("alice\tonline\nbob\taway\n"); on_presence(presence); netmsg_free(presence);
      QA(user_presence("alice") == 1 && user_presence("bob") == 2 && user_presence("hidden") == 0);
    }
    { BPTR f = Open("AmiRocket:avatar.png", MODE_OLDFILE); char bytes[4096]; LONG n = f ? Read(f,bytes,sizeof(bytes)) : -1;
      if (f) Close(f);
      QA(n > 0);
      if (n > 0 && DataTypesBase) {
          NetMsg *avatar; struct BitMap *bitmap;
          avatar_get("alice", 17);
          avatar = netmsg_new(NC_AVATAR); avatar->a_user = ar_strdup("alice"); avatar->a_interval = G.avatar_generation;
          buf_append(&avatar->avatar, bytes, (size_t)n); on_avatar(avatar); netmsg_free(avatar);
          bitmap = avatar_get("alice", 17);
          if (!bitmap) {
              Object *pic; struct BitMapHeader *h = NULL; struct BitMap *bm = NULL;
              ULONG scaled, laid;
              printf("avatar state: failed=%d size=%d bytes=%lu err=%ld DT=%d\n",G.avatars[0].failed,G.avatars[0].size,(unsigned long)G.avatars[0].bytes.len,(long)IoErr(),DataTypesBase->lib_Version);
              pic = NewDTObject((APTR)"AmiRocket:avatar.png", DTA_SourceType, DTST_FILE, DTA_GroupID,GID_PICTURE,
                     PDTA_DestMode,PMODE_V43, PDTA_Remap,TRUE, PDTA_Screen,(ULONG)G.scr, TAG_END);
              printf("file picture=%lu err=%ld\n",(unsigned long)pic,(long)IoErr());
              if (pic) {
                  GetDTAttrs(pic,PDTA_BitMapHeader,(ULONG)&h,TAG_END);
                  printf("header %d x %d\n", h?h->bmh_Width:0,h?h->bmh_Height:0);
                  scaled=DoMethod(pic,PDTM_SCALE,17,17,0);
                  laid=DoDTMethod(pic,NULL,NULL,DTM_PROCLAYOUT,NULL,TRUE);
                  GetDTAttrs(pic,PDTA_DestBitMap,(ULONG)&bm,TAG_END);
                  printf("scaled=%lu laid=%lu bm=%lu err=%ld\n",(unsigned long)scaled,(unsigned long)laid,(unsigned long)bm,(long)IoErr());
                  DisposeDTObject(pic);
              }
          }
          QA(bitmap != NULL);
          { NetMsg *image = netmsg_new(NC_IMAGE); int found = 0, row;
            G.images[0].url = ar_strdup(G.msgs.items[40].files[0].url); G.images[0].loading = 1;
            image->a_url = ar_strdup(G.images[0].url); image->a_interval = G.image_generation;
            buf_append(&image->avatar, bytes, n); on_image(image); netmsg_free(image);
            QA(G.images[0].expanded && G.images[0].bitmap != NULL);
            msgs_rebuild(0); draw_chat(); QA(G.chat.nimages == 1);
            for (row = 0; row < G.chat.n; row++) if (G.chat.v[row].kind == VL_IMAGE) found++;
            QA(found > 0);
            image_toggle(40, 0); draw_chat(); QA(!G.images[0].expanded && G.chat.nimages == 0);
            image_toggle(40, 0); draw_chat(); QA(G.images[0].expanded && G.chat.nimages == 1);
            G.prefs.inline_images = 0; draw_chat(); QA(G.chat.nimages == 0);
            G.prefs.inline_images = 1; images_reset(); msgs_rebuild(0); draw_chat();
          }
          { struct BitMap *large = avatar_get("alice", 32);
            QA(large != NULL && large != bitmap);
            QA(avatar_get("alice", 17) == bitmap);
          }
      }
    }
    SetGadgetAttrs((struct Gadget *)G.g[G_FILTER], G.win, NULL, STRINGA_TextVal, (ULONG)"bob", TAG_END);
    G.filter_changed = 1; input_events(); redraw_dirty();
    QA(G.side.n == 2 && G.side.r[1].kind == SR_ROOM && !strcmp(G.side.r[1].rid, "r2"));
    QA(strstr(G.side.r[1].label, "bob") != NULL && strstr(G.side.r[1].label, "+1") == NULL);
    SetGadgetAttrs((struct Gadget *)G.g[G_FILTER], G.win, NULL, STRINGA_TextVal, (ULONG)"", TAG_END);
    G.filter_changed = 1; input_events(); redraw_dirty();
    QA(G.side.n > 2);
    for (i = 0; i < G.side.n; i++)
        if (G.side.r[i].is_dm && !strcmp(G.side.r[i].avatar_user, "alice")) {
            QA(G.side.r[i].bitmap != NULL); QA(G.side.r[i].presence == 1);
        }
    { int n = G.side.n; on_side_click(32,0); redraw_dirty(); QA(G.side.n < n); on_side_click(32,0); redraw_dirty(); QA(G.side.n == n); }
    { char clipped[256]; struct RastPort rp = *G.win->RPort;
      if (G.chat_font) SetFont(&rp, G.chat_font);
      side_label_fit(&rp, "RetroFritzl, thebiga, trasher", 100, 1, clipped);
      QA(TextLength(&rp, (CONST_STRPTR)clipped, strlen(clipped)) <= 100);
      QA(strstr(clipped, "...") != NULL && strstr(clipped, "trasher") != NULL);
      side_label_fit(&rp, "RetroFritzl, thebiga, trasher", 500, 1, clipped);
      QA(!strcmp(clipped, "RetroFritzl, thebiga, trasher"));
      side_label_fit(&rp, "long label", 1, 0, clipped); QA(!clipped[0]);
      for (i = 0; i < G.side.n; i++)
          if (G.side.r[i].kind == SR_ROOM && !strcmp(G.side.r[i].rid, "r2")) QA(G.side.r[i].participants == 3);
      if (G.chat_font) QA(G.side.row_h == G.chat_font->tf_YSize + 5);
    }
    ui_font = G.win->RPort->Font;
    scroll_chat(50); QA(G.win->RPort->Font == ui_font);
    row = G.chat.top; strcpy(anchor, G.msgs.items[G.chat.v[row].msg_index].id);
    older = netmsg_new(NC_HISTORY); strcpy(older->rid, "r1"); older->a_interval = 7;
    older->msgs.items = ar_calloc(3, sizeof(RcMsg)); older->msgs.count = older->msgs.cap = 3;
    for (i = 0; i < 3; i++) qa_message(&older->msgs.items[i], i);
    on_older(older); redraw_dirty();
    QA(G.msgs.count == 63);
    QA(!strcmp(G.msgs.items[G.chat.v[G.chat.top].msg_index].id, anchor));
    netmsg_free(older);
    for (row = 0; row < G.chat.n && G.chat.v[row].file_index < 0; row++);
    QA(row < G.chat.n);
    G.chat.top = row; G.chat.follow = 0; draw_chat();
    QA(chat_file_hit(&G.chat, 0, &message, &file) && file == 0 && G.msgs.items[message].file_count == 1);
    /* A stale history reply must not affect another room/generation. */
    older = netmsg_new(NC_HISTORY); strcpy(older->rid, "r1"); older->a_interval = 6;
    G.history_pending = 1; on_older(older); QA(G.history_pending == 1); netmsg_free(older);
    G.history_pending = 0;
    /* Reopening on another palette must retain the multiline draft. */
    composer_set("keep\nmy draft", 5); composer_capture();
    apply_theme(theme_find("kick13"));
    QA(G.win != NULL);
    if (G.win) { rooms_rebuild(); msgs_rebuild(0); update_title(); redraw_dirty(); composer_capture(); QA(!strncmp(G.hook_buf, "keep\nmy draft", 13)); }
    if (G.win) {
        char *long_draft = ar_malloc(6001);
        if (long_draft) {
            STRPTR full;
            memset(long_draft, 'a', 6000); long_draft[6000] = 0;
            composer_set(long_draft, 0); composer_capture(); QA(G.composer_overlong);
            apply_theme(theme_find("mocha"));
            full = (STRPTR)DoGadgetMethod((struct Gadget *)G.g[G_INPUT], G.win, NULL, GM_TEXTEDITOR_ExportText, NULL);
            QA(full && strlen((char *)full) >= 6000);
            if (full) FreeVec(full);
            ar_free(long_draft); composer_set("keep\nmy draft", 5);
        } else QA(0);
    }

    qa_trace("QA Workbench iconify and restore cycles\n");
    { Object *object=G.win_obj; int cycle, messages=G.msgs.count; char room[64],reply[128];
      ar_strlcpy(room,G.cur_rid,sizeof(room)); composer_set("iconify\nkeep my draft",8);
      QA(G.app_port && G.app_icon);
      for(cycle=0;cycle<3;cycle++) {
          QA(main_iconify() && G.iconified && !G.win && G.win_obj==object);
          QA(!conversation_visible());
          set_status("Background updates continue"); msgs_rebuild(0); redraw_dirty();
          QA(main_restore() && !G.iconified && G.win && G.win_obj==object);
          composer_capture(); QA(!strcmp(G.hook_buf,"iconify\nkeep my draft") && G.hook_pos==8);
          QA(G.msgs.count==messages && !strcmp(G.cur_rid,room));
      }
      QA(control_command("ICONIFY",reply,sizeof(reply))==0 && G.iconified);
      composer_set("updated while hidden",6);
      QA(control_command("SHOW",reply,sizeof(reply))==0 && G.win && !G.iconified);
      composer_capture(); QA(!strcmp(G.hook_buf,"updated while hidden") && G.hook_pos==6);
      QA(control_command("SHOW",reply,sizeof(reply))==0 && G.win);
      if(argc>1 && !strcmp(argv[1],"ICONIFY")) {
          QA(main_iconify()); qa_trace("QA visual Workbench AppIcon\n");
          for(cycle=0;cycle<1500 && !G.quit;cycle++) { handle_main(); if(G.dirty) redraw_dirty(); Delay(2); }
          QA(main_restore());
      }
    }
    qa_unread_attachments();
    qa_trace("QA worker retry backoff and expired credentials\n");
    qa_fail+=worker_recovery_qa(&qa_pass);
    qa_extensions();
    qa_servers(argc>1 && !strcmp(argv[1],"SERVERS"));
    qa_dock_processes();
    if (argc > 1 && strcmp(argv[1],"SERVERS") && strcmp(argv[1],"PREFS") && strcmp(argv[1],"ICONIFY") && G.win) {
        RcMsg *last = &G.msgs.items[G.msgs.count - 1];
        ar_free(last->text); last->text = ar_strdup("Code keeps indentation and literal characters:\n```yaml\nalias: Hallway lights\ntriggers:\n  - platform: device\n    type: turn_on\n    value: '*literal* :smile:'\n```\nOpen https://example.com/docs?section=amiga or [documentation](https://docs.example/guide). ");
        G.copy_chat = 0; composer_set("", 0);
        composer_capture(); close_main(); strcpy(G.prefs.theme, "mocha");
        G.prefs.win_width = 640; G.prefs.win_height = 400;
        if (open_main() == 0) { rooms_rebuild(); msgs_rebuild(1); update_title(); handle_main(); redraw_dirty(); for (i = 0; i < 2000 && !G.quit; i++) { handle_main(); input_events(); if (G.dirty) redraw_dirty(); Delay(2); } }
    }
done:
    images_reset(); close_main();
    rc_msglist_free(&G.msgs); rc_roomlist_free(&G.rooms); chat_free(&G.chat); side_free(&G.side);
    if (G.chat_font) CloseFont(G.chat_font);
    if (G.code_font) CloseFont(G.code_font);
    if (G.port) DeleteMsgPort(G.port);
    avatar_reset(); close_libs(); ar_mem_cleanup();
    printf("GUI checks: %d passed, %d failed\n", qa_pass, qa_fail);
    return qa_fail ? 5 : 0;
}

/* The CLI launcher may provide only 8 KiB. Run the harness synchronously
 * in a process with an explicit stack, rather than relying on __stack. */
static struct Task *qa_parent;
static BYTE qa_signal;
static volatile int qa_result;
static int qa_argc;
static char **qa_argv;
static void qa_entry(void)
{
    qa_result = qa_run(qa_argc, qa_argv);
    Forbid();
    Signal(qa_parent, 1UL << qa_signal);
    /* DOS unwinds the process before the parent can release this code. */
}
int main(int argc, char **argv)
{
    struct Process *child;
    qa_parent = FindTask(NULL); qa_signal = AllocSignal(-1);
    if (qa_signal < 0) return 20;
    qa_argc = argc; qa_argv = argv;
    child = CreateNewProcTags(NP_Entry, (ULONG)qa_entry, NP_StackSize, 262144,
        NP_Name, (ULONG)"AmiRocket GUI checks", NP_Input, Input(), NP_Output, Output(),
        NP_CloseInput, FALSE, NP_CloseOutput, FALSE, TAG_END);
    if (!child) { FreeSignal(qa_signal); return 20; }
    Wait(1UL << qa_signal); FreeSignal(qa_signal); return qa_result;
}

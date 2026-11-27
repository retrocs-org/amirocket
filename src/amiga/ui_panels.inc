#include <images/bevel.h>
/* Sectioned native dialogs. Included by gui.c to share its screen and worker event pump. */
static void window_cleanup_trace(const char *s)
{
#ifdef AR_GUI_QA
    Write(Output(),(APTR)s,strlen(s));
#else
    (void)s;
#endif
}
static Object *panel_button(int id, const char *text)
{
    return ButtonObject, GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text, End;
}
static Object *panel_check(int id, const char *text, int selected)
{
    return CheckBoxObject, GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text, GA_Selected, selected, End;
}
static Object *panel_label(const char *text)
{
    return LabelObject, LABEL_Text, (ULONG)text, End;
}
static Object *panel_text(const char *text)
{
    return ButtonObject, GA_ReadOnly, TRUE, BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Justification, BCJ_LEFT,
        BUTTON_RenderImage, (ULONG)LabelObject, LABEL_Text, (ULONG)text, LABEL_Justification, LJ_LEFT, End, End;
}
static Object *panel_window(Object *root, const char *title, int width)
{
    return WindowObject, WA_Title, (ULONG)title, WA_PubScreen, (ULONG)G.scr,
        WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE, WA_SizeGadget, TRUE,
        WA_Activate, TRUE, WA_Width, width, WINDOW_Position, WPOS_CENTERSCREEN,
        WA_IDCMP, IDCMP_CLOSEWINDOW|IDCMP_GADGETUP|IDCMP_GADGETDOWN|IDCMP_RAWKEY|IDCMP_VANILLAKEY|IDCMP_NEWSIZE|IDCMP_REFRESHWINDOW|IDCMP_IDCMPUPDATE,
        WINDOW_ParentGroup, (ULONG)root, End;
}
static void panel_wait(Object *obj)
{
    ULONG mask = 0, sig;
    sync_conversation_focus();
    GetAttr(WINDOW_SigMask, obj, &mask);
    sig = Wait(mask | client_tick_signal() | arexx_signal() | prefs_service_signal(G.prefs_service) | (1UL << G.port->mp_SigBit) | SIGBREAKF_CTRL_C);
    if (sig & arexx_signal()) arexx_handle();
    if (!G.transfer_dialog && (sig & prefs_service_signal(G.prefs_service))) handle_preferences();
    if(sig & client_tick_signal()) client_tick_handle();
    if (sig & SIGBREAKF_CTRL_C) G.quit = 1;
    if (sig & (1UL << G.port->mp_SigBit)) handle_port();
    if (G.dirty) redraw_dirty();
}


#include "plugin_catalog.inc"
static struct ColumnInfo plugin_columns[]={{48,"Plugin",CIF_WEIGHTED},{16,"Version",CIF_WEIGHTED},{36,"State",CIF_WEIGHTED},{-1,NULL,0}};
enum { P_EMOJI, P_AVATARS, P_IMAGES, P_INTERVAL, P_FONT, P_SIZE, P_SCREEN, P_LINKS, P_BROWSER, P_VIEWER, P_PREVIEW, P_SPACING, P_BACKGROUND, P_CACHE, P_MEMLOW, P_MEMCOUNT, P_MEMIMAGE, P_REALTIME, P_PREVIEWSIZE, P_EXTERNAL, P_AUTOIMAGES, P_CACHEDIR, P_DOWNLOADDIR, P_SOUND, P_FIELDS };
typedef struct {
    Object *obj, *root, *page, *field[P_FIELDS], *theme_button, *plugin_list, *plugin_details[3], *plugin_toggle;
    Object *server_list; struct List server_nodes;
    int server_slots[PREFS_SERVERS_MAX],server_count,server_selected;
    PluginCatalog *catalog; int selected;
    char details[3][160];
    struct Window *win;
    Prefs *plugin_draft;
    int theme,direct_edit;
    char preview_size[16], interval[16], size[16], spacing[16], background[16], memcount[16], memimage[16], diagnostics[2304];
    Object *network_diagnostics, *network_label,*diagnostic_copy;
} PrefsPanel;
#include "server_prefs.inc"
static void prefs_plugin_select(PrefsPanel *p,int index)
{
    PluginEntry *e; int i;
    if(index<0 || index>=p->catalog->count) return;
    p->selected=index; e=&p->catalog->entry[index];
    snprintf(p->details[0],sizeof(p->details[0]),"%s / %s",e->author,e->version);
    snprintf(p->details[1],sizeof(p->details[1]),"%s",e->about);
    snprintf(p->details[2],sizeof(p->details[2]),"%s",e->path);
    for(i=0;i<3;i++) SetGadgetAttrs((struct Gadget *)p->plugin_details[i],p->win,NULL,STRINGA_TextVal,(ULONG)p->details[i],TAG_END);
    if(p->win) { RethinkLayout((struct Gadget *)p->root,p->win,NULL,TRUE); RefreshGList((struct Gadget *)p->root,p->win,NULL,1); }
}
static void prefs_plugin_rows(PrefsPanel *p)
{
    SetGadgetAttrs((struct Gadget *)p->plugin_list,p->win,NULL,LISTBROWSER_Labels,~0UL,TAG_END);
    catalog_rows(p->catalog,p->plugin_draft);
    SetGadgetAttrs((struct Gadget *)p->plugin_list,p->win,NULL,LISTBROWSER_Labels,(ULONG)&p->catalog->nodes,LISTBROWSER_Selected,p->selected,TAG_END);
}
static void prefs_plugin_settings(PrefsPanel *p)
{
    PluginEntry *e; Object *root,*obj,*option,*value,*path; struct Window *win;
    char key[80]; int done=0;
    if(!p->catalog->count) return;
    e=&p->catalog->entry[p->selected]; snprintf(key,sizeof(key),"plugin.%s.text",e->id);
    root=VLayoutObject, LAYOUT_SpaceOuter,TRUE, LAYOUT_VertSpacing,8,
        LAYOUT_AddChild,panel_text(e->name), CHILD_WeightedHeight,0,
        LAYOUT_AddChild,path=mk_string(270,e->path,255,0), CHILD_Label,panel_label("Module path"), CHILD_WeightedHeight,0,
        LAYOUT_AddChild,option=mk_string(271,"text",31,0), CHILD_Label,panel_label("Option"), CHILD_WeightedHeight,0,
        LAYOUT_AddChild,value=mk_string(272,prefs_extra(p->plugin_draft,key,!strcmp(e->id,"quickreply")?"Hello from AmiRocket!":""),255,0), CHILD_Label,panel_label("Value"), CHILD_WeightedHeight,0,
        LAYOUT_AddChild,panel_text("Options are staged until Use or Save.\nSee the plugin documentation for option names."), CHILD_WeightedHeight,0,
        LAYOUT_AddChild,HLayoutObject,
            LAYOUT_AddChild,panel_button(273,"Stage settings"),
            LAYOUT_AddChild,panel_button(274,"Cancel"),
        End, CHILD_WeightedHeight,0,
    End;
    if(!root) return;
    obj=panel_window(root,"Plugin settings",560); if(!obj) { DisposeObject(root); return; }
    win=RA_OpenWindow(obj); if(!win) { DisposeObject(obj); return; }
    while(!done && !G.quit) {
        ULONG result; UWORD code;
        while((result=RA_HandleInput(obj,&code))!=WMHI_LASTMSG) {
            int id=result&WMHI_GADGETMASK;
            if((result&WMHI_CLASSMASK)==WMHI_CLOSEWINDOW) done=1;
            if((result&WMHI_CLASSMASK)!=WMHI_GADGETUP) continue;
            if(id==274) done=1;
            if(id==273) {
                const char *name=str_of(option); const char *v=str_of(value); const char *module=str_of(path);
                if(!*name || strchr(name,'.') || strlen(name)>31 || strpbrk(module,"\r\n")) { request("Plugin settings","Enter a simple option name and a valid module path."); continue; }
                snprintf(key,sizeof(key),"plugin.%s.%s",e->id,name);
                if(prefs_set(p->plugin_draft,key,v)<0) { request("Plugin settings","Cannot store this option; check its name and value."); continue; }
                snprintf(key,sizeof(key),"plugin.%s.path",e->id);
                if(prefs_set(p->plugin_draft,key,module)<0) { request("Plugin settings","Cannot store the module path."); continue; }
                ar_strlcpy(e->path,module,sizeof(e->path)); done=1;
            }
        }
        if(!done) panel_wait(obj);
    }
    DisposeObject(obj); prefs_plugin_select(p,p->selected); prefs_plugin_rows(p);
}
static int prefs_panel_build(PrefsPanel *p)
{
    Object *root; int i;
    memset(p, 0, sizeof(*p)); NewList(&p->server_nodes); p->server_selected=-1;
    p->plugin_draft=ar_malloc(sizeof(Prefs)); if(!p->plugin_draft) return 0; *p->plugin_draft=G.prefs;
    prefs_migrate_servers(p->plugin_draft); prefs_servers_rows(p);
    p->catalog=ar_malloc(sizeof(*p->catalog)); if(!p->catalog) return 0;
    catalog_scan(p->catalog,p->plugin_draft); catalog_rows(p->catalog,p->plugin_draft);
    p->selected=0;
    strcpy(p->details[0],"Select a plugin to view its details.");
    strcpy(p->details[1]," "); strcpy(p->details[2]," ");
    p->theme = theme_find(G.prefs.theme);
    snprintf(p->preview_size,sizeof(p->preview_size),"%d",G.prefs.preview_width);
    snprintf(p->interval, sizeof(p->interval), "%d", G.prefs.interval);
    snprintf(p->memcount,sizeof(p->memcount),"%d",G.prefs.message_limit);
    snprintf(p->memimage,sizeof(p->memimage),"%d",G.prefs.image_cache_kib);
    snprintf(p->spacing,sizeof(p->spacing),"%d",G.prefs.request_spacing_ms);
    snprintf(p->background,sizeof(p->background),"%d",G.prefs.background_interval);
    snprintf(p->size, sizeof(p->size), "%d", G.prefs.chat_font_size);
    if(!prefs_runtime_network(p->diagnostics,sizeof(p->diagnostics)) || !p->diagnostics[0])
        ar_strlcpy(p->diagnostics,"No running client traffic yet.",sizeof(p->diagnostics));
    root = p->root = VLayoutObject, LAYOUT_SpaceOuter, TRUE, LAYOUT_VertSpacing, 6,
        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, VLayoutObject,
                LAYOUT_AddChild, panel_button(106, "Servers"), CHILD_WeightedHeight,0,
                LAYOUT_AddChild, panel_button(100, "General"), CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, panel_button(101, "Appearance"), CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, panel_button(102, "Plugins"), CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, panel_button(103, "Helpers"), CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, panel_button(107, "Network"), CHILD_WeightedHeight,0,
                LAYOUT_AddChild,panel_button(108,"Memory"), CHILD_WeightedHeight,0,
                LAYOUT_AddChild,panel_button(109,"Notifications"),CHILD_WeightedHeight,0,
                LAYOUT_AddChild, panel_button(104, "Diagnostics"), CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, panel_button(105, "About"), CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, SpaceObject, End,
            End, CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, p->page = PageObject, PAGE_FixedVert, TRUE, PAGE_FixedHoriz, TRUE,
                PAGE_Add, VLayoutObject, LAYOUT_SpaceOuter, TRUE, LAYOUT_VertSpacing, 6,
                    LAYOUT_AddChild, panel_text("General / performance"), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, p->field[P_INTERVAL] = mk_string(210, p->interval, 4, 0),
                    CHILD_Label, panel_label("Poll interval (2-600 seconds)"), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, p->field[P_EMOJI] = panel_check(211, "Draw emoji images", G.prefs.show_emoji), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, p->field[P_AVATARS] = panel_check(212, "Show / download user avatars", G.prefs.show_avatars), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, p->field[P_IMAGES] = panel_check(213, "Enable thumbnail previews (off: text only)", G.prefs.inline_images), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild,p->field[P_AUTOIMAGES]=panel_check(215,"Load visible thumbnails automatically",G.prefs.auto_thumbnails),CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,p->field[P_PREVIEWSIZE]=mk_string(214,p->preview_size,4,0),CHILD_Label,panel_label("Preview width (160 / 320 / 640)"),CHILD_WeightedHeight,0,
                    LAYOUT_AddChild, panel_text("Disable graphics or increase the interval\nto reduce memory and network use.\nAutomatic previews are off by default."), CHILD_WeightedHeight,0,
                    LAYOUT_AddChild, SpaceObject, End,
                End,
                PAGE_Add, VLayoutObject, LAYOUT_SpaceOuter, TRUE, LAYOUT_VertSpacing, 6,
                    LAYOUT_AddChild, panel_text("Appearance"), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, p->theme_button = panel_button(220, theme_get(p->theme)->name),
                    CHILD_Label, panel_label("Theme (click to cycle)"), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, p->field[P_SCREEN] = panel_check(221, "Theme on its own screen", G.prefs.ownscreen), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, p->field[P_FONT] = mk_string(222, G.prefs.chat_font, 127, 0),
                    CHILD_Label, panel_label("Chat font (empty = screen font)"), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, p->field[P_SIZE] = mk_string(223, p->size, 3, 0),
                    CHILD_Label, panel_label("Font size (8-48)"), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, panel_text("Example: helvetica.font or topaz.font\nCode blocks use a fixed-width font."), CHILD_WeightedHeight,0,
                    LAYOUT_AddChild, SpaceObject, End,
                End,
                PAGE_Add, VLayoutObject, LAYOUT_SpaceOuter, TRUE, LAYOUT_VertSpacing, 6,
                    LAYOUT_AddChild, p->plugin_list=ListBrowserObject, GA_ID,260, GA_RelVerify,TRUE,
                        LISTBROWSER_Labels,(ULONG)&p->catalog->nodes, LISTBROWSER_ColumnInfo,(ULONG)plugin_columns,
                        LISTBROWSER_ColumnTitles,TRUE, LISTBROWSER_AutoFit,TRUE, LISTBROWSER_Selected,0, End,
                        CHILD_MinHeight,60,
                    LAYOUT_AddChild, VLayoutObject, LAYOUT_VertSpacing,2,
                        LAYOUT_AddChild,p->plugin_details[0]=StringObject, GA_ReadOnly,TRUE, GA_TabCycle,FALSE, STRINGA_MaxChars,256, STRINGA_TextVal,(ULONG)p->details[0], End, CHILD_WeightedHeight,0,
                        LAYOUT_AddChild,p->plugin_details[1]=StringObject, GA_ReadOnly,TRUE, GA_TabCycle,FALSE, STRINGA_MaxChars,256, STRINGA_TextVal,(ULONG)p->details[1], End, CHILD_WeightedHeight,0,
                        LAYOUT_AddChild,p->plugin_details[2]=StringObject, GA_ReadOnly,TRUE, GA_TabCycle,FALSE, STRINGA_MaxChars,256, STRINGA_TextVal,(ULONG)p->details[2], End, CHILD_WeightedHeight,0,
                    End, CHILD_WeightedHeight,0,
                    LAYOUT_AddChild, HLayoutObject,
                        LAYOUT_AddChild, p->plugin_toggle=panel_button(261,"Enable / disable"),
                    End, CHILD_WeightedHeight,0,
                    LAYOUT_AddChild, panel_button(262,"Plugin settings..."), CHILD_WeightedHeight,0,
                    LAYOUT_AddChild, panel_text("Enable or disable, then choose Use or Save.\nDisabled plugins are never executed for inspection."), CHILD_WeightedHeight,0,
                End,
                PAGE_Add, VLayoutObject, LAYOUT_SpaceOuter, TRUE, LAYOUT_VertSpacing, 6,
                    LAYOUT_AddChild, panel_text("External helpers"), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, p->field[P_LINKS] = panel_check(230, "Enable web link opening", G.prefs.open_links), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, p->field[P_BROWSER] = mk_string(231, G.prefs.browser, 127, 0),
                    CHILD_Label, panel_label("Browser helper executable"), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, p->field[P_VIEWER] = mk_string(232, G.prefs.image_viewer, 127, 0),
                    CHILD_Label, panel_label("Image viewer executable"), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild,p->field[P_CACHEDIR]=mk_string(235,G.prefs.cache_dir,255,0),CHILD_Label,panel_label("Media cache drawer"),CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,p->field[P_DOWNLOADDIR]=mk_string(236,G.prefs.download_dir,255,0),CHILD_Label,panel_label("Default download drawer"),CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,p->field[P_EXTERNAL]=panel_check(234,"Open full-size images in external viewer",G.prefs.external_images),CHILD_WeightedHeight,0,
                    LAYOUT_AddChild, p->field[P_PREVIEW] = panel_check(233, "Open saved images in viewer", G.prefs.preview_images), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, panel_text("Configure OpenURL's browser separately.\nNative plugin modules are configured\nin the Plugins section."), CHILD_WeightedHeight,0,
                    LAYOUT_AddChild, SpaceObject, End,
                End,
                PAGE_Add, VLayoutObject, LAYOUT_SpaceOuter, TRUE, LAYOUT_VertSpacing, 6,
                    LAYOUT_AddChild, panel_text("Diagnostics"), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, p->network_diagnostics=ButtonObject, GA_ReadOnly,TRUE, BUTTON_BevelStyle,BVS_NONE, BUTTON_Justification,BCJ_LEFT,
                        BUTTON_RenderImage,(ULONG)(p->network_label=LabelObject,LABEL_Text,(ULONG)p->diagnostics,LABEL_Justification,LJ_LEFT,End),End, CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,panel_button(292,"Refresh network statistics"), CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,p->diagnostic_copy=panel_button(293,"Copy diagnostic report"),CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,panel_text("Statistics are sampled every 5 seconds.\nRequests/min is a sliding estimate; HTTP 0\nmeans a transport failure. Queues never\nautomatically replay failed message sends."), CHILD_WeightedHeight,0,
                    LAYOUT_AddChild, SpaceObject, End,
                End,
                PAGE_Add, VLayoutObject, LAYOUT_SpaceOuter, TRUE, LAYOUT_VertSpacing, 6,
                    LAYOUT_AddChild, panel_text("AmiRocket " AR_VERSION), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, panel_text("Rocket.Chat for AmigaOS 3.2\nReAction interface / AmiSSL networking\nUse applies to running clients only.\nSave applies now and keeps the configuration."), CHILD_WeightedHeight,0,
                    LAYOUT_AddChild, SpaceObject, End,
                End,
                PAGE_Add, VLayoutObject, LAYOUT_SpaceOuter,TRUE, LAYOUT_VertSpacing,8,
                    LAYOUT_AddChild,panel_text("Rocket.Chat servers"), CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,p->server_list=ListBrowserObject, GA_ID,280, GA_RelVerify,TRUE,
                        LISTBROWSER_Labels,(ULONG)&p->server_nodes, LISTBROWSER_ColumnInfo,(ULONG)server_columns,
                        LISTBROWSER_ColumnTitles,TRUE, LISTBROWSER_AutoFit,TRUE, LISTBROWSER_Selected,p->server_count?0:~0UL, End,
                        CHILD_MinHeight,80,
                    LAYOUT_AddChild,HLayoutObject, LAYOUT_EvenSize,TRUE,
                        LAYOUT_AddChild,panel_button(281,"Add..."),
                        LAYOUT_AddChild,panel_button(282,"Edit..."),
                        LAYOUT_AddChild,panel_button(283,"Remove"),
                    End, CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,panel_button(284,"Toggle auto-connect"), CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,panel_text("Each server keeps its own account and rooms.\nUse applies this list now; Save also keeps it.\nAuto-connect uses a remembered login on startup."), CHILD_WeightedHeight,0,
                End,
                PAGE_Add, VLayoutObject, LAYOUT_SpaceOuter,TRUE, LAYOUT_VertSpacing,8,
                    LAYOUT_AddChild,panel_text("Network / proxy protection"), CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,p->field[P_REALTIME]=panel_check(299,"Realtime WebSocket updates",G.prefs.realtime), CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,p->field[P_SPACING]=mk_string(293,p->spacing,5,0),
                        CHILD_Label,panel_label("Request gap (100-5000 ms)"), CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,p->field[P_BACKGROUND]=mk_string(294,p->background,4,0),
                        CHILD_Label,panel_label("Idle polling (15-600 seconds)"), CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,p->field[P_CACHE]=panel_check(295,"Cache avatars on disk (24 hours)",G.prefs.avatar_disk_cache), CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,panel_text("Limits apply separately to each server.\n429/503 Retry-After pauses all requests.\nRealtime coalesces events; REST catches up.\nAvatar cache: 64 slots, up to 8 MiB.\nClear the selected cache to remove avatars."), CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,SpaceObject,End,
                End,
                PAGE_Add,VLayoutObject,LAYOUT_SpaceOuter,TRUE,LAYOUT_VertSpacing,6,
                    LAYOUT_AddChild,panel_text("Memory / cache limits"),CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,panel_button(309,"Use low-resource preset"),CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,p->field[P_MEMLOW]=panel_check(296,"Low-memory mode",G.prefs.low_memory),CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,p->field[P_MEMCOUNT]=mk_string(297,p->memcount,5,0),CHILD_Label,panel_label("Messages per server (50-1000)"),CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,p->field[P_MEMIMAGE]=mk_string(298,p->memimage,5,0),CHILD_Label,panel_label("Image cache (256-8192 KiB)"),CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,panel_text("Low memory: at most 200 messages,\navatar/emoji drawing off, two image previews\nand 512 KiB of encoded image cache.\nDownloads are limited to 2 MiB.\nUse Room / Clear media caches in the client."),CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,SpaceObject,End,
                End,
                PAGE_Add,VLayoutObject,LAYOUT_SpaceOuter,TRUE,LAYOUT_VertSpacing,8,
                    LAYOUT_AddChild,panel_text("Notification sounds"),CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,p->field[P_SOUND]=mk_string(237,G.prefs.notification_sound,255,0),CHILD_Label,panel_label("Sound file (blank: system beep)"),CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,HLayoutObject,LAYOUT_AddChild,panel_button(238,"Test sound"),LAYOUT_AddChild,panel_button(239,"Use bundled sound"),End,CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,panel_text("Uses installed sound datatypes, for example 8SVX.\nShort files up to 256 KiB; played once.\nEnable sounds per server under Servers / Edit.\nQuiet mode and mention-only rules still apply.\nAlerts are limited to one sound every 2 seconds."),CHILD_WeightedHeight,0,
                    LAYOUT_AddChild,SpaceObject,End,
                End,
            End,
        End,
        LAYOUT_AddChild, HLayoutObject, LAYOUT_EvenSize, TRUE,
            LAYOUT_AddChild, panel_button(300, "Revert"),
            LAYOUT_AddChild, panel_button(303, "Use"),
            LAYOUT_AddChild, panel_button(301, "Save"),
            LAYOUT_AddChild, panel_button(302, "Cancel"),
        End, CHILD_WeightedHeight, 0,
    End;
    if (!root) return 0;
    p->obj = panel_window(root, "AmiRocket Preferences - General", 620);
    if (!p->obj) { DisposeObject(root); return 0; }
    SetAttrs(p->obj, WA_Height, G.scr->Height > 330 ? 300 : G.scr->Height - 32, TAG_END);
    p->win = RA_OpenWindow(p->obj);
    if (!p->win) { DisposeObject(p->obj); p->obj = NULL; return 0; }
    for (i = 0; i < P_FIELDS; i++) if (!p->field[i]) return 0;
    prefs_plugin_select(p,0);
    return 1;
}
static void prefs_panel_select(PrefsPanel *p, int section)
{
    SetGadgetAttrs((struct Gadget *)p->page, p->win, NULL, PAGE_Current, section, TAG_END);
    RethinkLayout((struct Gadget *)p->root, p->win, NULL, TRUE);
    RefreshGList((struct Gadget *)p->root, p->win, NULL, 1);
}
static int prefs_panel_read(PrefsPanel *p, Prefs *out)
{
    int interval = atoi(str_of(p->field[P_INTERVAL])), size = atoi(str_of(p->field[P_SIZE]));
    if (interval < 2 || interval > 600 || size < 8 || size > 48) return 0;
    if(atoi(str_of(p->field[P_SPACING]))<100 || atoi(str_of(p->field[P_SPACING]))>5000 ||
       atoi(str_of(p->field[P_BACKGROUND]))<15 || atoi(str_of(p->field[P_BACKGROUND]))>600) return 0;
    *out = *p->plugin_draft;
    if(prefs_set(out,"message_limit",str_of(p->field[P_MEMCOUNT])) || prefs_set(out,"image_cache_kib",str_of(p->field[P_MEMIMAGE]))) return 0;
    if(prefs_set(out,"preview_width",str_of(p->field[P_PREVIEWSIZE]))) return 0;
    out->low_memory=checked(p->field[P_MEMLOW]);
    out->realtime=checked(p->field[P_REALTIME]);
    out->request_spacing_ms=atoi(str_of(p->field[P_SPACING])); out->background_interval=atoi(str_of(p->field[P_BACKGROUND]));
    out->avatar_disk_cache=checked(p->field[P_CACHE]); out->interval = interval; out->chat_font_size = size;
    out->show_emoji = checked(p->field[P_EMOJI]); out->show_avatars = checked(p->field[P_AVATARS]);
    out->inline_images = checked(p->field[P_IMAGES]); out->open_links = checked(p->field[P_LINKS]);
    if(prefs_set(out,"notification_sound",str_of(p->field[P_SOUND])))return 0;
    if(prefs_set(out,"cache_dir",str_of(p->field[P_CACHEDIR])) || prefs_set(out,"download_dir",str_of(p->field[P_DOWNLOADDIR])))return 0;
    out->auto_thumbnails=checked(p->field[P_AUTOIMAGES]);
    out->external_images=checked(p->field[P_EXTERNAL]);
    out->ownscreen = checked(p->field[P_SCREEN]); out->preview_images = checked(p->field[P_PREVIEW]);
    ar_strlcpy(out->theme, theme_get(p->theme)->id, sizeof(out->theme));
    ar_strlcpy(out->chat_font, str_of(p->field[P_FONT]), sizeof(out->chat_font));
    ar_strlcpy(out->browser, str_of(p->field[P_BROWSER]), sizeof(out->browser));
    ar_strlcpy(out->image_viewer, str_of(p->field[P_VIEWER]), sizeof(out->image_viewer));
    return !strpbrk(out->browser, "\r\n") && !strpbrk(out->image_viewer, "\r\n");
}
static void prefs_panel_dispose(PrefsPanel *p)
{
    if(p->obj) DisposeObject(p->obj);
    free_list(&p->server_nodes);
    if(p->catalog) { free_list(&p->catalog->nodes); ar_free(p->catalog); p->catalog=NULL; }
    if(p->plugin_draft) { memset(p->plugin_draft,0,sizeof(Prefs)); ar_free(p->plugin_draft); }
    p->obj=NULL; p->plugin_draft=NULL;
}
static int prefs_ui_equal(const Prefs *a,const Prefs *b)
{
    static const char *keys[]={"notification_sound","cache_dir","download_dir","auto_thumbnails","external_images","preview_width","realtime","low_memory","message_limit","image_cache_kib","request_spacing_ms","background_interval","avatar_disk_cache","interval","theme","ownscreen","chat_font","chat_font_size","preview_images","show_emoji","show_avatars","inline_images","open_links","browser","image_viewer",NULL};
    char av[256],bv[256]; int i;
    for(i=0;keys[i];i++) if(prefs_get(a,keys[i],av,sizeof(av)) || prefs_get(b,keys[i],bv,sizeof(bv)) || strcmp(av,bv)) return 0;
    for(i=0;i<PREFS_SERVERS_MAX;i++) if(!prefs_server_equal(&a->servers[i],&b->servers[i])) return 0;
    for(i=0;i<a->extra_count;i++) if(!strncmp(a->extra[i].key,"plugin.",7) && strcmp(a->extra[i].value,prefs_extra(b,a->extra[i].key,""))) return 0;
    for(i=0;i<b->extra_count;i++) if(!strncmp(b->extra[i].key,"plugin.",7) && strcmp(b->extra[i].value,prefs_extra(a,b->extra[i].key,""))) return 0;
    return 1;
}
static void prefs_panel_low_resource(PrefsPanel *panel)
{
    prefs_low_resource(panel->plugin_draft);
    SetGadgetAttrs((struct Gadget *)panel->field[P_MEMLOW],panel->win,NULL,GA_Selected,TRUE,TAG_END);
    SetGadgetAttrs((struct Gadget *)panel->field[P_REALTIME],panel->win,NULL,GA_Selected,TRUE,TAG_END);
    SetGadgetAttrs((struct Gadget *)panel->field[P_CACHE],panel->win,NULL,GA_Selected,TRUE,TAG_END);
    SetGadgetAttrs((struct Gadget *)panel->field[P_AVATARS],panel->win,NULL,GA_Selected,FALSE,TAG_END);
    SetGadgetAttrs((struct Gadget *)panel->field[P_EMOJI],panel->win,NULL,GA_Selected,FALSE,TAG_END);
    SetGadgetAttrs((struct Gadget *)panel->field[P_AUTOIMAGES],panel->win,NULL,GA_Selected,FALSE,TAG_END);
    SetGadgetAttrs((struct Gadget *)panel->field[P_MEMCOUNT],panel->win,NULL,STRINGA_TextVal,(ULONG)"100",TAG_END);
    SetGadgetAttrs((struct Gadget *)panel->field[P_MEMIMAGE],panel->win,NULL,STRINGA_TextVal,(ULONG)"256",TAG_END);
    SetGadgetAttrs((struct Gadget *)panel->field[P_PREVIEWSIZE],panel->win,NULL,STRINGA_TextVal,(ULONG)"160",TAG_END);
    SetGadgetAttrs((struct Gadget *)panel->field[P_INTERVAL],panel->win,NULL,STRINGA_TextVal,(ULONG)"60",TAG_END);
    SetGadgetAttrs((struct Gadget *)panel->field[P_BACKGROUND],panel->win,NULL,STRINGA_TextVal,(ULONG)"300",TAG_END);
    SetGadgetAttrs((struct Gadget *)panel->field[P_SPACING],panel->win,NULL,STRINGA_TextVal,(ULONG)"1500",TAG_END);
}
static void __attribute__((unused)) preferences_window(void)
{
    PrefsPanel panel; Prefs next; int done = 0, save = 0,section=0,dirty=0,temporary=0; char caption[160];
    static const char *titles[] = {"AmiRocket Preferences - General", "AmiRocket Preferences - Appearance", "AmiRocket Preferences - Plugins", "AmiRocket Preferences - Helpers", "AmiRocket Preferences - Diagnostics", "AmiRocket Preferences - About", "AmiRocket Preferences - Servers", "AmiRocket Preferences - Network", "AmiRocket Preferences - Memory","AmiRocket Preferences - Notifications"};
    if (!prefs_panel_build(&panel)) return;
    if(prefs_load(&next)==0) { prefs_migrate_servers(&next); temporary=!prefs_ui_equal(&G.prefs,&next); }
    G.panel_depth++;
    while (!done && !G.quit) {
        ULONG result; UWORD code;
        while ((result = RA_HandleInput(panel.obj, &code)) != WMHI_LASTMSG) {
            int id = result & WMHI_GADGETMASK;
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) done = 1;
            if ((result & WMHI_CLASSMASK) == WMHI_RAWKEY) {
                int key=result&WMHI_KEYMASK; if(key==0x45) { done=1; continue; }
                if(key==0x54) id=303; else if(key==0x55) id=301; else continue;
            } else if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
            if (id >= 100 && id <= 109) {
                section=id-100; prefs_panel_select(&panel, section);
            } else if(id==239) {SetGadgetAttrs((struct Gadget *)panel.field[P_SOUND],panel.win,NULL,STRINGA_TextVal,(ULONG)"PROGDIR:Sounds/Notification.8svx",TAG_END);
            } else if(id==238) {notification_sound_play(str_of(panel.field[P_SOUND]));
            } else if(id==293) {
                char report[2400];snprintf(report,sizeof(report),"AmiRocket " AR_VERSION " network report\n%s",panel.diagnostics);
                if(!clipboard_write(report))request("Diagnostics","Could not copy the report.");
                else SetGadgetAttrs((struct Gadget *)panel.diagnostic_copy,panel.win,NULL,GA_Text,(ULONG)"Report copied",TAG_END);
            } else if(id==309) {
                prefs_panel_low_resource(&panel);
            } else if(id==280) {
                ULONG row=~0UL; GetAttr(LISTBROWSER_Selected,panel.server_list,&row);
                if(row<(ULONG)panel.server_count) panel.server_selected=panel.server_slots[row];
            } else if(id==292) {
                SetGadgetAttrs((struct Gadget *)panel.diagnostic_copy,panel.win,NULL,GA_Text,(ULONG)"Copy diagnostic report",TAG_END);
                if(!prefs_runtime_network(panel.diagnostics,sizeof(panel.diagnostics))) ar_strlcpy(panel.diagnostics,"Client is not running.",sizeof(panel.diagnostics));
                SetAttrs(panel.network_label,LABEL_Text,(ULONG)panel.diagnostics,TAG_END);
                RethinkLayout((struct Gadget *)panel.root,panel.win,NULL,TRUE);
                RefreshGList((struct Gadget *)panel.root,panel.win,NULL,1);
            } else if(id==281 || id==282) prefs_server_edit(&panel,id==281);
            else if(id==283 && panel.server_selected>=0) {
                memset(&panel.plugin_draft->servers[panel.server_selected],0,sizeof(ServerPrefs));
                panel.plugin_draft->servers_defined=1; prefs_servers_rows(&panel);
            } else if(id==284 && panel.server_selected>=0) {
                ServerPrefs *server=&panel.plugin_draft->servers[panel.server_selected];
                server->autoconnect=!server->autoconnect; prefs_servers_rows(&panel);
            } else if (id == 220) {
                panel.theme = (panel.theme + 1) % theme_count();
                SetGadgetAttrs((struct Gadget *)panel.theme_button, panel.win, NULL, GA_Text, (ULONG)theme_get(panel.theme)->name, TAG_END);
            } else if(id==260) {
                ULONG selected=0; GetAttr(LISTBROWSER_Selected,panel.plugin_list,&selected); prefs_plugin_select(&panel,selected);
            } else if(id==261 && panel.catalog->count) {
                PluginEntry *e=&panel.catalog->entry[panel.selected]; char key[80];
                snprintf(key,sizeof(key),"plugin.%s.path",e->id); prefs_set(panel.plugin_draft,key,e->path);
                snprintf(key,sizeof(key),"plugin.%s.enabled",e->id);
                prefs_set(panel.plugin_draft,key,!strcmp(prefs_extra(panel.plugin_draft,key,"0"),"1")?"0":"1");
                prefs_plugin_rows(&panel);
            } else if(id==262) {
                prefs_plugin_settings(&panel);
            } else if (id == 301 || id == 303) {
                if (!prefs_panel_read(&panel, &next)) request("Preferences", "Check preference ranges:\nPoll 2-600s; font 8-48; gap 100-5000ms; idle 15-600s.\nMemory: 50-1000 messages, images 256-8192 KiB.");
                else if (id == 303 && !prefs_runtime_send(&next,0)) request("Preferences", "Use needs a running AmiRocket client. Save also works while the client is closed.");
                else { save = id == 301 ? 1 : 2; done = 1; }
            } else if (id == 302) done = 1;
            else if (id == 300) {
                prefs_panel_dispose(&panel);
                if (!prefs_panel_build(&panel)) return;
            }
        }
        dirty=!prefs_panel_read(&panel,&next) || !prefs_ui_equal(&next,&G.prefs);
        snprintf(caption,sizeof(caption),"%s%s",titles[section],dirty?" * Unsaved changes":temporary?" - Session settings (not saved)":" - Saved settings");
        SetWindowTitles(panel.win,(CONST_STRPTR)caption,(CONST_STRPTR)-1);
        if(!done) { Delay(5); if(SetSignal(0,SIGBREAKF_CTRL_C)&SIGBREAKF_CTRL_C) done=1; handle_port(); }
    }
    G.panel_depth--;
    prefs_panel_dispose(&panel);
    if (save == 1) {
        Prefs latest; int loaded;
        if(prefs_lock()<0) { request("Preferences","Could not lock configuration."); return; }
        loaded=prefs_load(&latest);
        if(loaded==-2) { prefs_unlock(); request("Preferences","Existing configuration exceeds supported limits; it has not been overwritten."); return; }
        if (!loaded) {
            ar_strlcpy(next.server,latest.server,sizeof(next.server)); ar_strlcpy(next.user,latest.user,sizeof(next.user));
            ar_strlcpy(next.user_id,latest.user_id,sizeof(next.user_id)); ar_strlcpy(next.token,latest.token,sizeof(next.token));
            next.remember = latest.remember; next.verify = latest.verify;
            { int i; for(i=0;i<PREFS_SERVERS_MAX;i++) {
                ServerPrefs *server=&next.servers[i]; const ServerPrefs *saved=&latest.servers[i];
                if(server->remember && !strcmp(server->url,saved->url) && !strcmp(server->user,saved->user)) {
                    ar_strlcpy(server->token,saved->token,sizeof(server->token)); ar_strlcpy(server->user_id,saved->user_id,sizeof(server->user_id));
                }
            } }

            { int i; for(i=0;i<latest.extra_count;i++) if(!strncmp(latest.extra[i].key,"workspace.",10)) prefs_set(&next,latest.extra[i].key,latest.extra[i].value); }
            next.win_left = latest.win_left; next.win_top = latest.win_top; next.win_width = latest.win_width; next.win_height = latest.win_height;
        }
        loaded=prefs_save(&next); prefs_unlock();
        if(loaded<0) request("Preferences", "Could not save preferences; existing configuration retained.");
        else prefs_runtime_send(&next,1);
    }
}

/* New channels and DMs share a compact dialog; commas create group DMs. */
static void new_conversation(int mode)
{
    Object *obj, *root, *name, *members, *kind;
    struct Window *win; int done = 0;
    static const char *labels[] = {"Direct message", "Public channel", "Private channel"};
    if (!G.connected) { request("New conversation", "Connect to a server first."); return; }
    root = VLayoutObject, LAYOUT_SpaceOuter, TRUE, LAYOUT_VertSpacing, 6,
        LAYOUT_AddChild, kind = panel_button(401, labels[mode]), CHILD_Label, panel_label("Type (click to cycle)"), CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, name = mk_string(402, "", 128, 0), CHILD_Label, panel_label("Channel name (channels only)"), CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, members = mk_string(403, "", 3000, 0), CHILD_Label, panel_label("People: comma-separated usernames"), CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, panel_text("Example: RetroFritzl, trasher\nOne person starts a DM; several start a group DM.\nChannel membership is optional; you are included."),
        LAYOUT_AddChild, HLayoutObject, LAYOUT_EvenSize, TRUE,
            LAYOUT_AddChild, panel_button(404, "Create / open"),
            LAYOUT_AddChild, panel_button(405, "Cancel"), End, CHILD_WeightedHeight, 0,
    End;
    if (!root) return;
    obj = panel_window(root, "AmiRocket - New conversation", 480);
    if (!obj) { DisposeObject(root); return; }
    win = RA_OpenWindow(obj); if (!win) { DisposeObject(obj); return; }
    G.panel_depth++;
    while (!done && !G.quit) {
        ULONG result; UWORD code;
        while ((result = RA_HandleInput(obj, &code)) != WMHI_LASTMSG) {
            int id = result & WMHI_GADGETMASK;
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) done = 1;
            if ((result & WMHI_CLASSMASK) == WMHI_RAWKEY) {
                int key=result&WMHI_KEYMASK; if(key==0x45) { done=1; continue; }
                if(key==0x54) id=303; else if(key==0x55) id=301; else continue;
            } else if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
            if (id == 401) { mode = (mode + 1) % 3; SetGadgetAttrs((struct Gadget *)kind, win, NULL, GA_Text, (ULONG)labels[mode], TAG_END); }
            else if (id == 405) done = 1;
            else if (id == 404) {
                const char *n = str_of(name), *users = str_of(members); NetMsg *m; Buf b;
                if ((!mode && !*users) || (mode && (!*n || strpbrk(n, " \t\r\n")))) {
                    request("New conversation", mode ? "Enter a channel name without spaces." : "Enter one or more usernames."); continue;
                }
                m = netmsg_new(NC_CREATE_ROOM); if (!m) continue;
                m->a_user = ar_strdup(!mode ? "d" : mode == 1 ? "c" : "p");
                buf_init(&b); latin1_to_utf8(&b, n); m->a_text = buf_detach(&b);
                latin1_to_utf8(&b, users); m->a_pass = buf_detach(&b);
                if (!m->a_user || !m->a_text || !m->a_pass) { netmsg_free(m); continue; }
                send_cmd(m); set_status("Creating / opening conversation..."); done = 1;
            }
        }
        if (!done) panel_wait(obj);
    }
    G.panel_depth--; DisposeObject(obj);
}

static void transfer_cancel_current(Object *cancel)
{
    if(G.transfer_cancelled)return;
    G.transfer_cancelled=1;worker_cancel_transfer(G.active_server,G.transfer_serial);
    SetGadgetAttrs((struct Gadget *)cancel,G.transfer_win,NULL,GA_Disabled,TRUE,TAG_END);
    SetGadgetAttrs((struct Gadget *)G.transfer_text,G.transfer_win,NULL,GA_Text,(ULONG)"Cancelling...",TAG_END);
}
/* A modal progress window leaves the worker running and pumps its replies. */
static void transfer_dialog(NetMsg *first)
{
    Object *obj,*root,*cancel; struct Window *win; char *url=ar_strdup(first->a_url),*path=ar_strdup(first->a_text?first->a_text:"");
    int preview=first->a_verify,retry=1,save=0,kind=first->kind,image_generation=first->a_interval; ar_ms budget=first->a_before;
    root=VLayoutObject, LAYOUT_SpaceOuter,TRUE, LAYOUT_VertSpacing,6,
        LAYOUT_AddChild,G.transfer_text=panel_button(701,"Connecting / waiting for server..."),
        LAYOUT_AddChild,G.transfer_gauge=FuelGaugeObject,FUELGAUGE_Min,0,FUELGAUGE_Max,100,FUELGAUGE_Level,0,FUELGAUGE_Percent,TRUE,End,CHILD_MinHeight,18,CHILD_WeightedHeight,0,
        LAYOUT_AddChild,panel_text(kind==NC_IMAGE?(G.prefs.low_memory?"Preview limits: 2 MiB / 250,000 pixels.\nCancel stops the transfer; partial files are not saved.":"Preview limits: 2 MiB / 1 million pixels.\nCancel stops the transfer; partial files are not saved."):(budget>0 && budget<=2UL*1024*1024?"Maximum file size: 2 MiB.\nCancel stops the transfer; partial files are not saved.":"Maximum file size: 8 MiB.\nCancel stops the transfer; partial files are not saved.")),
        LAYOUT_AddChild,cancel=panel_button(702,"Cancel"), End;
    obj=panel_window(root,kind==NC_UPLOAD?"Upload attachment":"Download attachment",390); win=obj?RA_OpenWindow(obj):NULL;
    if(!win || !url || !path) { if(kind==NC_IMAGE) image_loading_done(first->a_url); if(obj) DisposeObject(obj); else if(root) DisposeObject(root); netmsg_free(first); ar_free(url); ar_free(path); G.transfer_text=NULL;G.transfer_gauge=NULL; return; }
    G.panel_depth++; G.transfer_dialog=1; G.transfer_win=win;
    while(retry && !G.quit) {
        NetMsg *m=first; first=NULL;
        if(!m) { m=netmsg_new(kind); if(m) { m->a_url=ar_strdup(url); m->a_text=ar_strdup(path); m->a_verify=preview; m->a_interval=image_generation; m->a_before=budget; } }
        if(!m || !m->a_url || (kind==NC_DOWNLOAD && !m->a_text)) { netmsg_free(m); break; }
        G.transfer_serial++; if(!G.transfer_serial) G.transfer_serial++;
        G.transfer_started=amiga_local_ms();
        m->sent_at=G.transfer_serial; G.download_pending=1; G.transfer_rc=RC_OK;
        G.transfer_cancelled=G.transfer_save_offer=0; G.transfer_status[0]=0;
        SetGadgetAttrs((struct Gadget *)G.transfer_text,win,NULL,GA_Text,(ULONG)"Connecting / waiting for server...",GA_ReadOnly,TRUE,TAG_END);
        SetGadgetAttrs((struct Gadget *)cancel,win,NULL,GA_Disabled,FALSE,TAG_END);
        SetGadgetAttrs((struct Gadget *)G.transfer_gauge,win,NULL,FUELGAUGE_Level,0,TAG_END);
        send_cmd(m);
        while(G.download_pending) {
            ULONG r; UWORD code;
            while((r=RA_HandleInput(obj,&code))!=WMHI_LASTMSG) {
                if((r&WMHI_CLASSMASK)==WMHI_CLOSEWINDOW || ((r&WMHI_CLASSMASK)==WMHI_RAWKEY && (r&WMHI_KEYMASK)==0x45) || ((r&WMHI_CLASSMASK)==WMHI_GADGETUP && (r&WMHI_GADGETMASK)==702)) {
                    transfer_cancel_current(cancel);
                }
            }
            if(G.download_pending) panel_wait(obj);
            if(G.quit) worker_cancel_transfer(G.active_server,G.transfer_serial);
        }
        retry=0;
        if(G.transfer_save_offer && !G.quit) {
            struct EasyStruct es={sizeof(es),0,(STRPTR)"Image cannot be previewed",(STRPTR)"%s",(STRPTR)"Save attachment...|Close"};
            save=EasyRequest(win,&es,NULL,(ULONG)G.transfer_status)==1;
        } else if(G.transfer_rc!=RC_OK && !G.quit && !G.transfer_cancelled && strcmp(G.transfer_status,"Download cancelled")) {
            struct EasyStruct es={sizeof(es),0,(STRPTR)(kind==NC_UPLOAD?"Upload failed or unconfirmed":"Download failed"),(STRPTR)"%s",(STRPTR)(kind==NC_UPLOAD?"Close":"Retry|Close")};
            retry=EasyRequest(win,&es,NULL,(ULONG)G.transfer_status)==1 && kind!=NC_UPLOAD;
        }
    }
    G.panel_depth--; G.transfer_dialog=0; G.transfer_win=NULL; G.transfer_text=NULL;G.transfer_gauge=NULL;
    if(kind==NC_IMAGE) image_loading_done(url);
    DisposeObject(obj);
    if(save) { int i,f,done=0; for(i=0;i<G.msgs.count && !done;i++) for(f=0;f<G.msgs.items[i].file_count;f++)
        if(!strcmp(url,G.msgs.items[i].files[f].url) || !strcmp(url,rc_attachment_preview_url(&G.msgs.items[i].files[f]))) { done=1; download_attachment(i,f); break; } }
    ar_free(url); ar_free(path);
    handle_preferences(); arexx_handle();
}

#include "conversation_tools.inc"

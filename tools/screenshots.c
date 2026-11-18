/* Native website screenshot fixture. Sample data only; no network or saved settings. */
#define AR_GUI_QA 1
#include "../src/amiga/gui.c"
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>
#include <dos/dostags.h>
struct Library *CyberGfxBase;
static int capture(struct Window *w,const char *path)
{
    int width=w->Width,height=w->Height;UBYTE *rgb=ar_malloc(width*height*3);BPTR f;char header[80];int n;
    if(!rgb)return 0;
    Delay(20);
    ReadPixelArray(rgb,0,0,width*3,&w->WScreen->RastPort,w->LeftEdge,w->TopEdge,width,height,RECTFMT_RGB);
    f=Open((STRPTR)path,MODE_NEWFILE);if(!f){ar_free(rgb);return 0;}
    n=sprintf(header,"P6\n%d %d\n255\n",width,height);
    Write(f,header,n);n=Write(f,rgb,width*height*3);Close(f);ar_free(rgb);
    return n==width*height*3;
}
static int run(void)
{
    int i,ok=1;PrefsPanel panel;
    const char *names[]={"general","development","hardware","announcements","jamie","robin","jamie,robin,you"};
    const char *users[]={"jamie","robin","you","jamie"};
    const char *texts[]={"Workbench is up. Coffee is ready. Who is around?", "Right here on the Amiga. A familiar desktop, a new conversation.", "A small ARexx script for bringing the client to the front:\n```rexx\naddress AMIROCKET 'SHOW'\naddress AMIROCKET 'DOCKSTATUS'\n```", "Channels, private messages and multiple workspaces. All in one place."};
    puts("capture: start");fflush(stdout);
    if(ar_mem_init()<0)return 20;
    memset(&G,0,sizeof(G));G.dirty_sig=-1;
    if(open_libs()<0)return 20;
    CyberGfxBase=OpenLibrary("cybergraphics.library",0);if(!CyberGfxBase)return 20;
    puts("capture: libraries");fflush(stdout);
    prefs_defaults(&G.prefs);G.prefs.win_width=980;G.prefs.win_height=650;G.prefs.win_left=20;G.prefs.win_top=38;
    strcpy(G.prefs.theme,"mocha");G.prefs.chat_font_size=13;
    strcpy(G.prefs.servers[0].url,"https://amiga.example.org");strcpy(G.prefs.servers[0].name,"amiga.example.org");
    strcpy(G.prefs.servers[1].url,"https://community.example.org");strcpy(G.prefs.servers[1].name,"community.example.org");
    servers_init();G.server_view=0;G.history_exhausted=1;G.avatar_pending=1;G.multi=1;G.connected=1;G.servers[0].connected=1;G.servers[0].expanded=1;G.servers[1].expanded=0;
    strcpy(G.username,"you");strcpy(G.cur_rid,"room0");G.room_serial=1;
    puts("capture: fonts");fflush(stdout);
    load_chat_font();side_init(&G.side);chat_init(&G.chat);rc_roomlist_init(&G.rooms);rc_msglist_init(&G.msgs);G.port=CreateMsgPort();
    G.rooms.items=ar_calloc(7,sizeof(RcRoom));G.rooms.count=G.rooms.cap=7;
    for(i=0;i<7;i++){char id[24];RcRoom *r=&G.rooms.items[i];sprintf(id,"room%d",i);r->rid=ar_strdup(id);r->name=ar_strdup(names[i]);r->type=i<4?'c':'d';r->open=1;r->favorite=i<3;r->unread=i==1?2:0;}
    G.msgs.items=ar_calloc(4,sizeof(RcMsg));G.msgs.count=G.msgs.cap=4;
    for(i=0;i<4;i++){char id[24];RcMsg *m=&G.msgs.items[i];sprintf(id,"sample%d",i);m->id=ar_strdup(id);m->rid=ar_strdup("room0");m->username=ar_strdup(users[i]);m->text=ar_strdup(texts[i]);m->ts_ms=1795008000000LL+i*120000;m->updated_ms=m->ts_ms;}
    G.msgs.items[0].reactions=ar_strdup("coffee 2");
    puts("capture: open window");fflush(stdout);
    if(open_main()<0)return 20;
    puts("capture: window opened");fflush(stdout);
    handle_main();rooms_rebuild();msgs_rebuild(1);update_title();redraw_dirty();
    puts("capture: mocha");fflush(stdout);
    ok &= capture(G.win,"Shots:chat-mocha.ppm");
    apply_theme(theme_find("nord"));handle_main();redraw_dirty();ok &= capture(G.win,"Shots:chat-nord.ppm");
    if(prefs_panel_build(&panel)){prefs_panel_select(&panel,0);ok &= capture(panel.win,"Shots:preferences.ppm");prefs_panel_select(&panel,2);ok &= capture(panel.win,"Shots:plugins.ppm");prefs_panel_dispose(&panel);}else ok=0;
    close_main();rc_msglist_free(&G.msgs);rc_roomlist_free(&G.rooms);chat_free(&G.chat);side_free(&G.side);
    if(G.chat_font)CloseFont(G.chat_font);
    if(G.code_font)CloseFont(G.code_font);
    if(G.port)DeleteMsgPort(G.port);
    CloseLibrary(CyberGfxBase);close_libs();ar_mem_cleanup();printf("Native website screenshots: %s\n",ok?"captured":"failed");return ok?0:5;
}
static struct Task *parent;static BYTE sig;static int result;
static void entry(void){result=run();Forbid();Signal(parent,1UL<<sig);}
int main(void){struct Process *p;parent=FindTask(NULL);sig=AllocSignal(-1);if(sig<0)return 20;p=CreateNewProcTags(NP_Entry,(ULONG)entry,NP_StackSize,262144,NP_Name,(ULONG)"AmiRocket screenshot fixture",NP_Input,Input(),NP_Output,Output(),NP_CloseInput,FALSE,NP_CloseOutput,FALSE,TAG_END);if(!p)return 20;Wait(1UL<<sig);FreeSignal(sig);return result;}

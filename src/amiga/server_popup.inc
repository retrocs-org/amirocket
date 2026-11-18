/* Trap RMB only over server roots; the ordinary Amiga menu strip remains
 * available elsewhere. WFLG_RMBTRAP is a documented mutable window flag. */
static int sidebar_server_at(int x,int y)
{
    struct IBox box; int row;
    if(!G.multi || !area_box(G.g[G_ROOMS],&box) || x<box.Left || x>=box.Left+box.Width || y<box.Top || y>=box.Top+box.Height) return -1;
    row=side_hit(&G.side,y-box.Top);
    return row>=0 && G.side.r[row].kind==SR_SERVER ? G.side.r[row].server_slot : -1;
}
static void sidebar_pointer(struct IntuiMessage *im)
{
    int slot;
    if(!G.win || (im->Class!=IDCMP_MOUSEMOVE && im->Class!=IDCMP_MOUSEBUTTONS)) return;
    slot=sidebar_server_at(im->MouseX,im->MouseY);
    Forbid();
    if(slot>=0) G.win->Flags|=WFLG_RMBTRAP; else G.win->Flags&=~WFLG_RMBTRAP;
    Permit();
    if(slot>=0 && im->Class==IDCMP_MOUSEBUTTONS && im->Code==MENUDOWN) {
        G.side_menu_slot=slot; G.side_menu_pending=1;
        G.side_menu_x=G.win->LeftEdge+im->MouseX; G.side_menu_y=G.win->TopEdge+im->MouseY;
    }
}
static int server_disconnect(int slot)
{
    NetMsg *m;
    if(slot<0 || slot>=PREFS_SERVERS_MAX || !G.prefs.servers[slot].url[0]) return 10;
    m=netmsg_new(NC_DISCONNECT); if(!m) return 20;
    G.servers[slot].generation++;
    if(slot==G.active_server) { G.busy=0; went_offline("Disconnected"); }
    else {
        ServerState *s=&G.servers[slot];
        s->connected=s->busy=0; rc_roomlist_free(&s->rooms); rc_msglist_free(&s->msgs); s->cur_rid[0]=s->loading_rid[0]=0;
        server_event(slot,0,"Disconnected");
    }
    server_send(slot,m);
    rooms_rebuild(); notify_unread(); return 0;
}
static void server_context_action(int slot,int action)
{
    if(slot<0 || slot>=PREFS_SERVERS_MAX || !G.prefs.servers[slot].url[0]) return;
    if(action==612) { launch_preferences_for_server(slot); return; }
    server_select(slot); if(G.active_server!=slot) return; G.server_view=1; msgs_rebuild(1); rooms_rebuild(); update_title();
    if(action==610) server_connect(slot,1);
    else if(action==611) server_disconnect(slot);
}
static void server_context_popup(void)
{
    Object *root,*obj,*connect,*disconnect; struct Window *win;
    int slot=G.side_menu_slot,generation,choice=0,done=0,connected,busy;
    int x=G.side_menu_x,y=G.side_menu_y;
    G.side_menu_pending=0;
    if(slot<0 || slot>=PREFS_SERVERS_MAX || !G.prefs.servers[slot].url[0]) return;
    generation=G.servers[slot].generation;
    connected=slot==G.active_server?G.connected:G.servers[slot].connected;
    busy=slot==G.active_server?G.busy:G.servers[slot].busy;
    root=VLayoutObject, LAYOUT_SpaceOuter,TRUE, LAYOUT_VertSpacing,1,
        LAYOUT_AddChild,connect=panel_button(610,G.servers[slot].last_error?"Retry Now":"Connect"),
        LAYOUT_AddChild,disconnect=panel_button(611,"Disconnect"),
        LAYOUT_AddChild,panel_button(612,"Edit Server..."),
    End;
    if(!root) return;
    SetAttrs(connect,GA_Disabled,(connected && !G.servers[slot].last_error)||busy,TAG_END);
    SetAttrs(disconnect,GA_Disabled,!connected&&!busy&&!G.servers[slot].reconnecting,TAG_END);
    if(x+170>G.scr->Width) x=G.scr->Width-170;
    if(y+80>G.scr->Height) y=G.scr->Height-80;
    if(x<0) x=0;
    if(y<0) y=0;
    obj=WindowObject, WA_PubScreen,(ULONG)G.scr, WA_Borderless,TRUE,
        WA_Activate,TRUE, WA_RMBTrap,TRUE, WA_Left,x,WA_Top,y,WA_Width,170,
        WA_IDCMP,IDCMP_GADGETUP|IDCMP_RAWKEY|IDCMP_INACTIVEWINDOW,
        WINDOW_ParentGroup,(ULONG)root, End;
    if(!obj) { DisposeObject(root); return; }
    win=RA_OpenWindow(obj); if(!win) { DisposeObject(obj); return; }
    G.panel_depth++;
    while(!done && !G.quit) {
        ULONG result; UWORD code;
        while((result=RA_HandleInput(obj,&code))!=WMHI_LASTMSG) {
            ULONG type=result&WMHI_CLASSMASK;
            if(type==WMHI_INACTIVE || type==WMHI_CLOSEWINDOW) done=1;
            if(type==WMHI_RAWKEY) {
                int key=result&WMHI_KEYMASK;
                if(key==0x45) done=1;
                if(key==0x12) { choice=612; done=1; } /* E: edit */
                if(key==0x33 && !busy && (!connected || G.servers[slot].last_error)) { choice=610; done=1; } /* C */
                if(key==0x22 && (connected || busy || G.servers[slot].reconnecting)) { choice=611; done=1; } /* D */
            }
            if(type==WMHI_GADGETUP) { choice=result&WMHI_GADGETMASK; done=1; }
        }
        if(!done) panel_wait(obj);
    }
    G.panel_depth--; DisposeObject(obj);
    if(choice && generation==G.servers[slot].generation) server_context_action(slot,choice);
}

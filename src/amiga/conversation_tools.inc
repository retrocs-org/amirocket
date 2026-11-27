static void search_select(int selected)
{
    Buf text; RcMsg *m;
    if(selected<0 || selected>=G.search_results.count || !G.search_preview) return;
    G.search_selected=selected; m=&G.search_results.items[selected]; buf_init(&text); utf8_to_latin1(&text,m->text?m->text:"",1);
    SetGadgetAttrs((struct Gadget *)G.search_preview,G.search_win,NULL,GA_TEXTEDITOR_Contents,(ULONG)buf_str(&text),TAG_END);
    buf_free(&text);
}
static void on_search(NetMsg *m)
{
    int i; struct Node *node;
    if(!G.search_active || m->server_slot!=G.search_slot || m->server_generation!=G.search_generation ||
       m->a_before!=G.search_serial || strcmp(m->rid,G.search_room)) return;
    G.search_busy=0;
    if(m->rc!=RC_OK) { request_utf8("Search failed",m->text[0]?m->text:rc_strerror(m->rc)); return; }
    SetGadgetAttrs((struct Gadget *)G.search_list,G.search_win,NULL,LISTBROWSER_Labels,~0UL,TAG_END);
    free_list(&G.search_nodes); rc_msglist_free(&G.search_results); G.search_results=m->msgs; rc_msglist_init(&m->msgs);
    { Buf query; buf_init(&query); utf8_to_latin1(&query,m->a_text?m->a_text:"",0);
      if(!m->a_interval || strcmp(G.search_query,buf_str(&query))) G.search_back_count=0;
      else if(m->a_interval>G.search_offset) {
          if(G.search_back_count==64) { memmove(G.search_back,G.search_back+1,63*sizeof(int)); G.search_back_count=63; }
          G.search_back[G.search_back_count++]=G.search_offset;
      } else if(m->a_interval<G.search_offset && G.search_back_count) G.search_back_count--;
      ar_strlcpy(G.search_query,buf_str(&query),sizeof(G.search_query)); buf_free(&query);
    }
    G.search_total=m->a_background; G.search_offset=m->a_interval;
    if(!m->a_interval && G.search_results.count>0) G.search_step=G.search_results.count;
    for(i=0;i<G.search_results.count;i++) {
        Buf line; RcMsg *message=&G.search_results.items[i]; char date[16],hm[8];
        buf_init(&line); ms_format_date(message->ts_ms-G.tz_ms,date); ms_format_hm(message->ts_ms-G.tz_ms,hm);
        buf_printf(&line,"%s %s  ",date,hm); utf8_to_latin1(&line,message->username?message->username:"",0);
        node=AllocListBrowserNode(1,LBNA_Column,0,LBNCA_CopyText,TRUE,LBNCA_Text,(ULONG)buf_str(&line),TAG_END);
        if(node) AddTail(&G.search_nodes,node);
        buf_free(&line);
    }
    SetGadgetAttrs((struct Gadget *)G.search_list,G.search_win,NULL,LISTBROWSER_Labels,(ULONG)&G.search_nodes,LISTBROWSER_Selected,0,TAG_END);
    if(!G.search_results.count) ar_strlcpy(G.search_caption,G.search_offset?"No more results":"No matches",sizeof(G.search_caption));
    else if(G.search_total<0) snprintf(G.search_caption,sizeof(G.search_caption),"Results %d-%d",G.search_results.count?G.search_offset+1:0,G.search_offset+G.search_results.count);
    else snprintf(G.search_caption,sizeof(G.search_caption),"Results %d-%d of %d",G.search_results.count?G.search_offset+1:0,G.search_offset+G.search_results.count,G.search_total);
    SetGadgetAttrs((struct Gadget *)G.search_info,G.search_win,NULL,GA_Text,(ULONG)G.search_caption,TAG_END);
    SetGadgetAttrs((struct Gadget *)G.search_preview,G.search_win,NULL,GA_TEXTEDITOR_Contents,(ULONG)"",TAG_END);
    if(G.search_results.count) search_select(0);
}
static void search_request(const char *query,int offset)
{
    NetMsg *m; Buf utf8;
    if(G.search_busy || !query || !*query || offset<0) return;
    buf_init(&utf8); latin1_to_utf8(&utf8,query);
    if(utf8.oom || utf8.len>256) { buf_free(&utf8); request("Search","Use a search query of at most 256 UTF-8 bytes."); return; }
    m=netmsg_new(NC_SEARCH); if(!m) { buf_free(&utf8); return; }
    m->a_text=buf_detach(&utf8); m->a_rid=ar_strdup(G.search_room); m->a_interval=offset; m->a_before=++G.search_serial;
    if(!m->a_text || !m->a_rid) { netmsg_free(m); return; }
    G.search_busy=1;
    SetGadgetAttrs((struct Gadget *)G.search_info,G.search_win,NULL,GA_Text,(ULONG)"Searching / waiting for server...",TAG_END);
    server_send(G.search_slot,m);
}
static void search_dispose(Object *obj)
{
    G.search_active=0;G.search_serial++;
    window_cleanup_trace("QA search detach labels\n");
    if(G.search_list)SetAttrs(G.search_list,LISTBROWSER_Labels,~0UL,TAG_END);
    window_cleanup_trace("QA search free labels\n");
    free_list(&G.search_nodes);
    window_cleanup_trace("QA search dispose objects\n");
    if(obj)DisposeObject(obj);
    G.search_list=G.search_preview=G.search_info=NULL;G.search_win=NULL;
    window_cleanup_trace("QA search free results\n");
    rc_msglist_free(&G.search_results);
    window_cleanup_trace("QA search cleanup complete\n");
}
static void search_window(void)
{
    Object *obj,*root,*query; struct Window *win; int done=0;
    if(G.server_view || !G.connected || !G.cur_rid[0]) { request("Search","Open a connected conversation first."); return; }
    NewList(&G.search_nodes); rc_msglist_init(&G.search_results);
    G.search_slot=G.active_server; G.search_generation=G.servers[G.active_server].generation;
    ar_strlcpy(G.search_room,G.cur_rid,sizeof(G.search_room)); G.search_query[0]=0; G.search_offset=G.search_total=G.search_busy=G.search_back_count=0; G.search_step=25;
    root=VLayoutObject,LAYOUT_SpaceOuter,TRUE,LAYOUT_VertSpacing,6,
        LAYOUT_AddChild,HLayoutObject,
            LAYOUT_AddChild,query=mk_string(750,"",256,0),
            LAYOUT_AddChild,panel_button(751,"Search"),CHILD_WeightedWidth,0,
        End,CHILD_WeightedHeight,0,
        LAYOUT_AddChild,G.search_info=panel_button(756,"Enter a query, then Search (25 results per page)"),CHILD_WeightedHeight,0,
        LAYOUT_AddChild,G.search_list=ListBrowserObject,GA_ID,752,GA_RelVerify,TRUE,LISTBROWSER_Labels,(ULONG)&G.search_nodes,End,CHILD_MinHeight,70,
        LAYOUT_AddChild,G.search_preview=TextEditorObject,GA_ID,757,GA_TEXTEDITOR_ReadOnly,TRUE,GA_TEXTEDITOR_Contents,(ULONG)"",End,CHILD_MinHeight,90,
        LAYOUT_AddChild,HLayoutObject,LAYOUT_EvenSize,TRUE,
            LAYOUT_AddChild,panel_button(753,"Previous"),LAYOUT_AddChild,panel_button(754,"Next"),LAYOUT_AddChild,panel_button(755,"Close"),
        End,CHILD_WeightedHeight,0,
    End;
    obj=panel_window(root,"Search conversation",600); win=obj?RA_OpenWindow(obj):NULL;
    if(!win) { if(obj) DisposeObject(obj); else if(root) DisposeObject(root); return; }
    G.search_win=win; G.search_active=1; G.panel_depth++;
    while(!done && !G.quit) {
        ULONG r; UWORD code;
        while((r=RA_HandleInput(obj,&code))!=WMHI_LASTMSG) {
            int id=r&WMHI_GADGETMASK;
            if((r&WMHI_CLASSMASK)==WMHI_CLOSEWINDOW || ((r&WMHI_CLASSMASK)==WMHI_RAWKEY && (r&WMHI_KEYMASK)==0x45)) {done=1;continue;}
            if((r&WMHI_CLASSMASK)!=WMHI_GADGETUP) continue;
            if(id==755) done=1;
            else if(id==751 || id==750) search_request(str_of(query),0);
            else if(id==753 && G.search_offset>0) search_request(G.search_query,G.search_back_count?G.search_back[G.search_back_count-1]:(G.search_offset>G.search_step && G.search_step>0?G.search_offset-G.search_step:0));
            else if(id==754 && G.search_offset<1000000 && G.search_results.count>0 && (G.search_total<0 || G.search_offset+G.search_results.count<G.search_total)) search_request(G.search_query,G.search_offset+G.search_results.count);
            else if(id==752) { ULONG row=0; GetAttr(LISTBROWSER_Selected,G.search_list,&row); search_select(row); }
        }
        if(!done) panel_wait(obj);
    }
    G.panel_depth--;search_dispose(obj);
}
static void upload_attachment(void)
{
    struct FileRequester *requester; NetMsg *m; char path[512];
    if(!G.connected || G.server_view || !G.cur_rid[0] || G.download_pending) { request("Upload","Open a connected conversation and finish any current transfer first."); return; }
    requester=AllocAslRequestTags(ASL_FileRequest,ASLFR_Window,(ULONG)G.win,ASLFR_TitleText,(ULONG)"Upload attachment (maximum 8 MiB)",ASLFR_DoSaveMode,FALSE,TAG_END);
    if(!requester) return;
    if(!AslRequestTags(requester,TAG_END)) { FreeAslRequest(requester); return; }
    ar_strlcpy(path,requester->fr_Drawer,sizeof(path));
    if(!requester->fr_File[0] || !AddPart(path,requester->fr_File,sizeof(path))) { FreeAslRequest(requester); return; }
    FreeAslRequest(requester);
    m=netmsg_new(NC_UPLOAD); if(!m) return;
    m->a_url=ar_strdup(G.cur_rid); m->a_text=ar_strdup(path);
    if(!m->a_url || !m->a_text) { netmsg_free(m); return; }
    transfer_dialog(m);
}
static void clear_media_caches(void)
{
    char path[320],directory[280]; int i; BPTR lock;
    images_reset(); avatar_reset();
    ar_strlcpy(directory,G.prefs.cache_dir,sizeof(directory));if(!AddPart(directory,"Avatars",sizeof(directory)))return;
    if(prefs_lock()==0) {
        for(i=0;i<64;i++) { snprintf(path,sizeof(path),"%s/%02d.dat",directory,i); DeleteFile(path); }
        prefs_unlock();
    }
    lock=Lock(directory,ACCESS_READ); if(lock) UnLock(lock);
    msgs_rebuild(0); rooms_rebuild(); set_status("Memory caches cleared; disk avatar cache cleared where writable");
}

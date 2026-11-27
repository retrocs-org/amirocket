/* Explicit message actions, bound to a server generation and room. */
static int action_owned(void)
{ return G.action_user[0] && !strcmp(G.action_user,G.action_me); }
static void action_target(const RcMsg *m)
{
    Buf text; buf_init(&text); utf8_to_latin1(&text,m->text?m->text:"",1);
    ar_strlcpy(G.action_id,m->id?m->id:"",sizeof(G.action_id));
    ar_strlcpy(G.action_user,m->username?m->username:"",sizeof(G.action_user));
    SetGadgetAttrs((struct Gadget *)G.action_preview,G.action_win,NULL,GA_TEXTEDITOR_Contents,(ULONG)buf_str(&text),TAG_END);
    SetGadgetAttrs((struct Gadget *)G.action_edit,G.action_win,NULL,GA_Disabled,!action_owned(),TAG_END);
    SetGadgetAttrs((struct Gadget *)G.action_delete,G.action_win,NULL,GA_Disabled,!action_owned(),TAG_END);
    buf_free(&text);
}
static void action_request(int kind,const char *value,int option)
{
    NetMsg *m;
    if(G.action_busy || !G.action_active) return;
    if(G.action_generation!=G.servers[G.action_slot].generation) { request("Message","The server configuration changed. Reopen this window."); return; }
    m=netmsg_new(kind); if(!m) return;
    m->a_rid=ar_strdup(G.action_room); m->a_uid=ar_strdup(kind==NC_THREAD || kind==NC_THREAD_SEND?G.action_thread:G.action_id);
    m->a_text=value?ar_strdup(value):NULL; m->a_interval=kind==NC_THREAD?option:0; m->a_verify=option;
    m->a_before=++G.action_serial;
    if(!m->a_rid || !m->a_uid || (value && !m->a_text)) { netmsg_free(m); return; }
    G.action_busy=1; SetGadgetAttrs((struct Gadget *)G.action_info,G.action_win,NULL,GA_Text,(ULONG)"Waiting for server...",TAG_END);
    server_send(G.action_slot,m);
}
static void on_message_action(NetMsg *m)
{
    int i; struct Node *node;
    if(!G.action_active || m->server_slot!=G.action_slot || m->server_generation!=G.action_generation ||
       m->a_before!=G.action_serial || strcmp(m->rid,G.action_room)) return;
    G.action_busy=0;
    if(m->rc!=RC_OK) { SetGadgetAttrs((struct Gadget *)G.action_info,G.action_win,NULL,GA_Text,(ULONG)"Failed / unconfirmed; text retained",TAG_END); request_utf8("Message action",m->text[0]?m->text:rc_strerror(m->rc)); return; }
    if(m->kind==NC_THREAD) {
        SetGadgetAttrs((struct Gadget *)G.action_list,G.action_win,NULL,LISTBROWSER_Labels,~0UL,TAG_END);
        free_list(&G.action_nodes); rc_msglist_free(&G.action_results); G.action_results=m->msgs; rc_msglist_init(&m->msgs);
        G.action_offset=m->a_interval; G.action_total=m->a_background;
        for(i=0;i<G.action_results.count;i++) {
            Buf label; RcMsg *item=&G.action_results.items[i]; buf_init(&label);
            utf8_to_latin1(&label,item->username?item->username:"?",0); buf_puts(&label,": ");
            utf8_to_latin1(&label,item->text?item->text:"",0); if(label.len>180) label.data[180]=0;
            node=AllocListBrowserNode(1,LBNA_Column,0,LBNCA_CopyText,TRUE,LBNCA_Text,(ULONG)buf_str(&label),TAG_END);
            if(node) AddTail(&G.action_nodes,node);
            buf_free(&label);
        }
        SetGadgetAttrs((struct Gadget *)G.action_list,G.action_win,NULL,LISTBROWSER_Labels,(ULONG)&G.action_nodes,TAG_END);
        snprintf(G.action_caption,sizeof(G.action_caption),"Thread replies %d-%d%s",G.action_results.count?G.action_offset+1:0,G.action_offset+G.action_results.count,G.action_total<0?"":" (25 per page)");
        SetGadgetAttrs((struct Gadget *)G.action_info,G.action_win,NULL,GA_Text,(ULONG)G.action_caption,TAG_END);
    } else {
        if(m->kind==NC_EDIT || m->kind==NC_THREAD_SEND) {
            STRPTR current=(STRPTR)DoGadgetMethod((struct Gadget *)G.action_editor,G.action_win,NULL,GM_TEXTEDITOR_ExportText,NULL);
            Buf utf8; buf_init(&utf8); if(current) latin1_to_utf8(&utf8,current);
            if(current && m->a_text && !strcmp(buf_str(&utf8),m->a_text)) SetGadgetAttrs((struct Gadget *)G.action_editor,G.action_win,NULL,GA_TEXTEDITOR_Contents,(ULONG)"",TAG_END);
            if(current) FreeVec(current);
            buf_free(&utf8);
        }
        SetGadgetAttrs((struct Gadget *)G.action_info,G.action_win,NULL,GA_Text,(ULONG)"Completed; refresh thread to see changes",TAG_END);
        { NetMsg *poll=netmsg_new(NC_POLL_NOW); if(poll) server_send(G.action_slot,poll); }
    }
}
static Object *message_action_window(Object **emoji)
{
    Object *root,*obj;
    root=VLayoutObject,LAYOUT_SpaceOuter,TRUE,LAYOUT_VertSpacing,5,
        LAYOUT_AddChild,G.action_info=panel_button(793,"Selected message / thread"),CHILD_WeightedHeight,0,
        LAYOUT_AddChild,G.action_preview=TextEditorObject,GA_TEXTEDITOR_ReadOnly,TRUE,End,CHILD_MinHeight,60,
        LAYOUT_AddChild,HLayoutObject,
            LAYOUT_AddChild,panel_button(780,"Quote"),LAYOUT_AddChild,panel_button(781,"Load thread"),
            LAYOUT_AddChild,panel_button(782,"Previous"),LAYOUT_AddChild,panel_button(783,"Next"),
        End,CHILD_WeightedHeight,0,
        LAYOUT_AddChild,G.action_list=ListBrowserObject,GA_ID,784,GA_RelVerify,TRUE,LISTBROWSER_Labels,(ULONG)&G.action_nodes,End,CHILD_MinHeight,65,
        LAYOUT_AddChild,panel_text("Reply text or replacement text (nothing is sent until clicked):"),CHILD_WeightedHeight,0,
        LAYOUT_AddChild,G.action_editor=TextEditorObject,GA_TEXTEDITOR_ExportWrap,0,GA_TEXTEDITOR_LineEndingExport,LINEENDING_LF,End,CHILD_MinHeight,65,
        LAYOUT_AddChild,HLayoutObject,
            LAYOUT_AddChild,panel_button(785,"Reply in thread"),
            LAYOUT_AddChild,panel_button(792,"Load for editing"),
            LAYOUT_AddChild,G.action_edit=panel_button(786,"Save edit"),
            LAYOUT_AddChild,G.action_delete=panel_button(787,"Delete own message"),
        End,CHILD_WeightedHeight,0,
        LAYOUT_AddChild,HLayoutObject,
            LAYOUT_AddChild,*emoji=mk_string(788,"smile",64,0),CHILD_Label,panel_label("Reaction"),
            LAYOUT_AddChild,panel_button(789,"Add"),LAYOUT_AddChild,panel_button(790,"Remove"),LAYOUT_AddChild,panel_button(791,"Close"),
        End,CHILD_WeightedHeight,0,
    End;
    if(!root) return NULL;
    obj=panel_window(root,"Message and thread",640);
    if(!obj) DisposeObject(root);
    return obj;
}
static void action_dispose(Object *obj)
{
    G.action_active=0;G.action_serial++;
    window_cleanup_trace("QA action detach labels\n");
    if(G.action_list)SetAttrs(G.action_list,LISTBROWSER_Labels,~0UL,TAG_END);
    window_cleanup_trace("QA action free labels\n");
    free_list(&G.action_nodes);
    window_cleanup_trace("QA action dispose objects\n");
    if(obj)DisposeObject(obj);
    G.action_win=NULL;G.action_list=G.action_editor=G.action_preview=G.action_info=G.action_edit=G.action_delete=NULL;
    window_cleanup_trace("QA action free results\n");
    rc_msglist_free(&G.action_results);
    window_cleanup_trace("QA action cleanup complete\n");
}
static void message_actions(void)
{
    Object *obj,*emoji; struct Window *win; int i,done=0;
    RcMsg *selected=NULL;
    if(G.action_active || G.server_view || !G.connected || !G.copy_chat || strcmp(G.copy_rid,G.cur_rid)) { request("Message","Select a message first."); return; }
    for(i=0;i<G.msgs.count;i++) if(!strcmp(G.msgs.items[i].id,G.copy_id)) { selected=&G.msgs.items[i]; break; }
    if(!selected) return;
    G.action_slot=G.active_server; G.action_generation=G.servers[G.active_server].generation;
    ar_strlcpy(G.action_room,G.cur_rid,sizeof(G.action_room)); ar_strlcpy(G.action_me,G.username,sizeof(G.action_me));
    ar_strlcpy(G.action_thread,selected->tmid&&*selected->tmid?selected->tmid:selected->id,sizeof(G.action_thread));
    NewList(&G.action_nodes); rc_msglist_init(&G.action_results); G.action_offset=G.action_busy=0; G.action_total=-1;
    obj=message_action_window(&emoji); win=obj?RA_OpenWindow(obj):NULL;
    if(!win) { if(obj) DisposeObject(obj); return; }
    G.action_win=win; G.action_active=1; G.panel_depth++; action_target(selected);
    while(!done && !G.quit) {
        ULONG r; UWORD code;
        while((r=RA_HandleInput(obj,&code))!=WMHI_LASTMSG) {
            int id=r&WMHI_GADGETMASK;
            if((r&WMHI_CLASSMASK)==WMHI_CLOSEWINDOW || ((r&WMHI_CLASSMASK)==WMHI_RAWKEY && (r&WMHI_KEYMASK)==0x45)) {done=1;continue;}
            if((r&WMHI_CLASSMASK)!=WMHI_GADGETUP) continue;
            if(id==791) done=1;
            if(G.action_busy) continue;
            if(id==784) { ULONG row=0; GetAttr(LISTBROWSER_Selected,G.action_list,&row); if(row<(ULONG)G.action_results.count) action_target(&G.action_results.items[row]); }
            if(id==781) action_request(NC_THREAD,NULL,0);
            if(id==782 && G.action_offset>=25) action_request(NC_THREAD,NULL,G.action_offset-25);
            if(id==783 && G.action_results.count && (G.action_total<0 || G.action_offset+G.action_results.count<G.action_total)) action_request(NC_THREAD,NULL,G.action_offset+G.action_results.count);
            if(id==780) {
                STRPTR text=(STRPTR)DoGadgetMethod((struct Gadget *)G.action_preview,win,NULL,GM_TEXTEDITOR_ExportText,NULL); Buf quote; const char *p;
                buf_init(&quote); buf_printf(&quote,"@%s wrote:\n> ",G.action_user);
                for(p=text?(const char *)text:"";*p;p++) { buf_putc(&quote,*p); if(*p=='\n') buf_puts(&quote,"> "); } buf_puts(&quote,"\n\n");
                if(G.active_server!=G.action_slot || strcmp(G.cur_rid,G.action_room) || quote.oom || plugin_insert(NULL,buf_str(&quote))<0) request("Quote","Could not insert quote; current draft retained."); else done=1;
                if(text) FreeVec(text);
                buf_free(&quote);
            }
            if((id==785 || id==786) && (id!=786 || action_owned())) {
                STRPTR text=(STRPTR)DoGadgetMethod((struct Gadget *)G.action_editor,win,NULL,GM_TEXTEDITOR_ExportText,NULL); Buf utf8;
                buf_init(&utf8); if(text) latin1_to_utf8(&utf8,text);
                if(!utf8.len || utf8.oom || (text && strlen(text)>INPUT_MAX)) request("Message","Enter 1-4000 characters.");
                else action_request(id==785?NC_THREAD_SEND:NC_EDIT,buf_str(&utf8),0);
                if(text) FreeVec(text);
                buf_free(&utf8);
            }
            if(id==792 && action_owned()) {
                STRPTR text=(STRPTR)DoGadgetMethod((struct Gadget *)G.action_preview,win,NULL,GM_TEXTEDITOR_ExportText,NULL);
                if(text) { SetGadgetAttrs((struct Gadget *)G.action_editor,win,NULL,GA_TEXTEDITOR_Contents,(ULONG)text,TAG_END); FreeVec(text); }
            }
            if(id==787 && action_owned()) {
                struct EasyStruct confirm={sizeof(confirm),0,(STRPTR)"Delete message",(STRPTR)"Delete your selected message permanently?",(STRPTR)"Delete|Cancel"};
                if(EasyRequest(win,&confirm,NULL)==1) action_request(NC_DELETE,NULL,0);
            }
            if(id==789 || id==790) action_request(NC_REACT,str_of(emoji),id==789);
        }
        if(!done) panel_wait(obj);
    }
    G.panel_depth--;action_dispose(obj);
}

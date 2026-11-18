static struct ColumnInfo server_columns[]={{52,"Server",CIF_WEIGHTED},{28,"User",CIF_WEIGHTED},{20,"Auto-connect",CIF_WEIGHTED},{-1,NULL,0}};
static void prefs_servers_rows(PrefsPanel *p)
{
    int i,selected=-1; struct Node *node;
    if(p->win) SetGadgetAttrs((struct Gadget *)p->server_list,p->win,NULL,LISTBROWSER_Labels,~0UL,TAG_END);
    free_list(&p->server_nodes); p->server_count=0;
    for(i=0;i<PREFS_SERVERS_MAX;i++) {
        ServerPrefs *s=&p->plugin_draft->servers[i];
        if(!s->url[0]) continue;
        if(i==p->server_selected) selected=p->server_count;
        p->server_slots[p->server_count++]=i;
        node=AllocListBrowserNode(3,LBNA_Column,0,LBNCA_CopyText,TRUE,LBNCA_Text,(ULONG)(s->name[0]?s->name:s->url),
            LBNA_Column,1,LBNCA_CopyText,TRUE,LBNCA_Text,(ULONG)s->user,
            LBNA_Column,2,LBNCA_CopyText,TRUE,LBNCA_Text,(ULONG)(s->autoconnect?"Yes":"No"),TAG_END);
        if(node) AddTail(&p->server_nodes,node);
    }
    if(selected<0 && p->server_count) { selected=0; p->server_selected=p->server_slots[0]; }
    if(!p->server_count) p->server_selected=-1;
    if(p->win) SetGadgetAttrs((struct Gadget *)p->server_list,p->win,NULL,LISTBROWSER_Labels,(ULONG)&p->server_nodes,LISTBROWSER_Selected,selected,TAG_END);
}
static void prefs_server_edit(PrefsPanel *p,int add)
{
    int slot=p->server_selected,done=0,i; ServerPrefs draft; char error[160];
    Object *root,*obj,*name,*url,*user,*auto_connect,*verify,*remember,*footer; struct Window *win; char caption[100];
    if(add) { for(slot=0;slot<PREFS_SERVERS_MAX && p->plugin_draft->servers[slot].url[0];slot++); }
    if(slot<0) return;
    if(slot==PREFS_SERVERS_MAX) { request("Servers","Up to eight servers can be configured."); return; }
    draft=p->plugin_draft->servers[slot];
    if(add) { memset(&draft,0,sizeof(draft)); draft.verify=draft.remember=1; strcpy(draft.url,"https://"); }
    if(p->direct_edit) footer=HLayoutObject,
        LAYOUT_AddChild,panel_button(303,"Use"), LAYOUT_AddChild,panel_button(301,"Save"), LAYOUT_AddChild,panel_button(297,"Cancel"), End;
    else footer=HLayoutObject,
        LAYOUT_AddChild,panel_button(296,"Apply to server list"), LAYOUT_AddChild,panel_button(297,"Cancel"), End;
    root=VLayoutObject, LAYOUT_SpaceOuter,TRUE, LAYOUT_VertSpacing,8,
        LAYOUT_AddChild,name=mk_string(290,draft.name,95,0), CHILD_Label,panel_label("Display name"), CHILD_WeightedHeight,0,
        LAYOUT_AddChild,url=mk_string(291,draft.url,255,0), CHILD_Label,panel_label("Server URL"), CHILD_WeightedHeight,0,
        LAYOUT_AddChild,user=mk_string(292,draft.user,95,0), CHILD_Label,panel_label("User name"), CHILD_WeightedHeight,0,
        LAYOUT_AddChild,auto_connect=panel_check(293,"Auto-connect on startup",draft.autoconnect), CHILD_WeightedHeight,0,
        LAYOUT_AddChild,verify=panel_check(294,"Verify TLS certificate",draft.verify), CHILD_WeightedHeight,0,
        LAYOUT_AddChild,remember=panel_check(295,"Remember login for this server",draft.remember), CHILD_WeightedHeight,0,
        LAYOUT_AddChild,panel_text("Sign in from the client to remember a login.\nPasswords are never stored in Preferences."), CHILD_WeightedHeight,0,
        LAYOUT_AddChild,footer, CHILD_WeightedHeight,0,
    End;
    if(!root) return;
    obj=panel_window(root,add?"Add Rocket.Chat server":"Edit Rocket.Chat server",560);
    if(!obj) { DisposeObject(root); return; }
    win=RA_OpenWindow(obj); if(!win) { DisposeObject(obj); return; }
    while(!done && !G.quit) {
        ULONG result; UWORD code;
        while((result=RA_HandleInput(obj,&code))!=WMHI_LASTMSG) {
            int id=result&WMHI_GADGETMASK;
            if((result&WMHI_CLASSMASK)==WMHI_CLOSEWINDOW) done=1;
            if((result&WMHI_CLASSMASK)==WMHI_RAWKEY) {
                int key=result&WMHI_KEYMASK;
                if(key==0x45) { done=1; continue; }
                if(p->direct_edit && key==0x54) id=303;
                else if(p->direct_edit && key==0x55) id=301; else continue;
            } else if((result&WMHI_CLASSMASK)!=WMHI_GADGETUP) continue;
            if(id==297) done=1;
            if(id==296 || (p->direct_edit && (id==301 || id==303))) {
                ServerPrefs *old=&p->plugin_draft->servers[slot];
                ar_strlcpy(draft.name,str_of(name),sizeof(draft.name)); ar_trim(draft.name);
                ar_strlcpy(draft.url,str_of(url),sizeof(draft.url)); ar_trim(draft.url);
                ar_strlcpy(draft.user,str_of(user),sizeof(draft.user)); ar_trim(draft.user);
                draft.autoconnect=checked(auto_connect); draft.verify=checked(verify); draft.remember=checked(remember);
                if(prefs_server_validate(&draft,error,sizeof(error))<0) { request("Server",error); continue; }
                for(i=0;i<PREFS_SERVERS_MAX;i++) if(i!=slot && !strcmp(p->plugin_draft->servers[i].url,draft.url) && !strcmp(p->plugin_draft->servers[i].user,draft.user)) break;
                if(i<PREFS_SERVERS_MAX) { request("Server","This server and account are already in the list."); continue; }
                if(strcmp(old->url,draft.url) || strcmp(old->user,draft.user) || !draft.remember) draft.token[0]=draft.user_id[0]=0;
                if(p->direct_edit) {
                    if(id==301 && prefs_save_server(slot,&draft)<0) { request("Server","Could not save this server. Existing configuration retained."); continue; }
                    if(!prefs_runtime_send_server(slot,&draft,id==301) && id==303) { request("Server","Use needs a running AmiRocket client."); continue; }
                }
                *old=draft; p->plugin_draft->servers_defined=1; p->server_selected=slot; done=1;
            }
        }
        if(!done) {
            int changed=strcmp(str_of(name),draft.name)||strcmp(str_of(url),draft.url)||strcmp(str_of(user),draft.user)||checked(auto_connect)!=draft.autoconnect||checked(verify)!=draft.verify||checked(remember)!=draft.remember;
            snprintf(caption,sizeof(caption),"%s%s",add?"Add Rocket.Chat server":"Edit Rocket.Chat server",changed?" * Unsaved changes":""); SetWindowTitles(win,(CONST_STRPTR)caption,(CONST_STRPTR)-1);
            Delay(5); handle_port();
        }
    }
    DisposeObject(obj); memset(&draft,0,sizeof(draft)); if(!p->direct_edit) prefs_servers_rows(p);
}

static int __attribute__((unused)) preferences_edit_server(int slot)
{
    PrefsPanel panel;
    if(slot<0 || slot>=PREFS_SERVERS_MAX || !G.prefs.servers[slot].url[0]) { request("Edit server","This server no longer exists."); return RETURN_FAIL; }
    memset(&panel,0,sizeof(panel)); NewList(&panel.server_nodes);
    panel.plugin_draft=ar_malloc(sizeof(Prefs)); if(!panel.plugin_draft) return RETURN_FAIL;
    *panel.plugin_draft=G.prefs; panel.server_selected=slot; panel.direct_edit=1;
    prefs_server_edit(&panel,0);
    memset(panel.plugin_draft,0,sizeof(Prefs)); ar_free(panel.plugin_draft); return RETURN_OK;
}

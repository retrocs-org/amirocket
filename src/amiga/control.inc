/* ARexx command bridge. All commands run on the GUI task; networking stays asynchronous. */
static const char *control_word(const char *p,char *out,int size)
{
    int n=0; while(*p==' '||*p=='\t') p++;
    while(*p && *p!=' ' && *p!='\t') { if(n+1<size) out[n++]=*p; p++; }
    out[n]=0; while(*p==' '||*p=='\t') p++; return p;
}
static void control_value(const char *p,char *out,int size)
{
    int n=0; size_t len=strlen(p); int quoted=len>=2 && p[0]=='"' && p[len-1]=='"';
    if(quoted) { p++; len-=2; }
    while(len && n+1<size) {
        if(quoted && len>=2 && p[0]=='"' && p[1]=='"') { p++; len--; }
        out[n++]=*p++; len--;
    }
    out[n]=0;
}
static int control_create(char type,const char *name,const char *members)
{
    NetMsg *m; Buf b;
    if(!G.connected || (type=='d' ? !*members : !*name)) return 10;
    m=netmsg_new(NC_CREATE_ROOM); if(!m) return 20;
    m->a_user=ar_strdup(type=='d'?"d":type=='p'?"p":"c"); buf_init(&b);
    latin1_to_utf8(&b,name); m->a_text=buf_detach(&b); latin1_to_utf8(&b,members); m->a_pass=buf_detach(&b);
    if(!m->a_user||!m->a_text||!m->a_pass) { netmsg_free(m); return 20; }
    send_cmd(m); return 0;
}

static int control_result(Buf *b,char *out,int size)
{
    int rc=b->oom || b->len>=(size_t)size?20:0;
    if(!rc) ar_strlcpy(out,buf_str(b),size);
    buf_free(b); return rc;
}
static int control_slot(const char *p,int *slot,const char **tail)
{
    char word[32],*end; long n;
    *tail=control_word(p,word,sizeof(word));
    if(!word[0]) return 10;
    n=strtol(word,&end,10);
    if(*end || n<1 || n>PREFS_SERVERS_MAX || !G.multi || !G.prefs.servers[n-1].url[0]) return 10;
    *slot=n-1; return 0;
}
static void control_summary(Buf *b)
{
    int slot,servers=0,connected=0,alerts=0;
    for(slot=0;slot<PREFS_SERVERS_MAX;slot++) if(G.prefs.servers[slot].url[0]) {
        servers++; if(!strcmp(dock_server_state(slot),"connected")) connected++; alerts+=dock_alerts(slot);
    }
    buf_printf(b,"version=2 epoch=%s seq=%lu dropped=%lu event_pending=%d connected=%d connected_servers=%d servers=%d unread=%d alerts=%d selected=%d selected_connected=%d selected_unread=%d iconified=%d temporary=%d pending=%d port=%s",
        arexx_epoch(),(unsigned long)arexx_sequence(),(unsigned long)arexx_dropped(),arexx_pending(),connected>0,connected,servers,dock_total_unread(),alerts,G.active_server+1,
        !strcmp(dock_server_state(G.active_server),"connected"),dock_unread(G.active_server),G.iconified,G.session_override,G.pending,arexx_port_name());
    buf_printf(b," transfer=%d",G.transfer_dialog!=0);
}
static void control_quoted(Buf *b,const char *text,int limit)
{
    int n=0; buf_putc(b,'"');
    while(text && *text && n++<limit) { unsigned char c=*text++; if(c=='"') buf_putc(b,'"'); buf_putc(b,c<32?' ':c); }
    buf_putc(b,'"');
}
static int control_rooms(const char *rest,char *out,int size)
{
    int slot,i,end; long offset=0; char *after; const char *tail; const RcRoomList *rooms; Buf b;
    if(control_slot(rest,&slot,&tail)) return 10;
    if(*tail) { offset=strtol(tail,&after,10); while(*after==' ' || *after=='\t') after++; if(*after || offset<0) return 10; }
    rooms=dock_rooms(slot); if(offset>rooms->count) return 10;
    end=offset+8<rooms->count?offset+8:rooms->count;
    buf_init(&b); buf_printf(&b,"epoch=%s seq=%lu generation=%d next=%d\n",arexx_epoch(),(unsigned long)arexx_sequence(),G.servers[slot].generation,end<rooms->count?end:-1);
    for(i=(int)offset;i<end;i++) {
        const RcRoom *r=&rooms->items[i];
        buf_puts(&b,"room\t"); control_quoted(&b,r->rid,128);
        buf_printf(&b,"\t%d\t%d\t%c\t",r->unread>0?r->unread:0,!!r->alert,r->type);
        control_quoted(&b,rc_room_title(r),128); buf_putc(&b,'\n');
    }
    return control_result(&b,out,size);
}
static int control_connection(const char *verb,const char *rest,char *out,int size)
{
    int slot=G.active_server,all=!ar_strcasecmp(rest,"ALL"),i,n=0,rc=0,connect=!strcmp(verb,"CONNECT"); const char *tail=rest;
    if(!G.multi || !G.running_client) return 10;
    if(!all && *rest && (control_slot(rest,&slot,&tail) || *tail)) return 10;
    if(!all && !G.prefs.servers[slot].url[0]) return 10;
    /* CONNECT never opens a password dialog or changes the selected server. */
    if(connect) for(i=0;i<PREFS_SERVERS_MAX;i++) if((all || i==slot) && G.prefs.servers[i].url[0]) {
        const ServerPrefs *p=&G.prefs.servers[i]; const char *state=dock_server_state(i);
        if(strcmp(state,"connected") && strcmp(state,"connecting") && (!p->remember || !p->user_id[0] || !p->token[0])) {
            server_event(i,RC_ERR_AUTH,"Login required"); notify_unread(); return 10;
        }
    }
    for(i=0;i<PREFS_SERVERS_MAX;i++) if((all || i==slot) && G.prefs.servers[i].url[0]) {
        int r=0; n++;
        if(connect) { if(strcmp(dock_server_state(i),"connected")) r=server_connect(i,0); }
        else r=server_disconnect(i);
        if(r>rc) rc=r;
    }
    if(!n) return 10;
    if(!rc) snprintf(out,size,"QUEUED servers=%d",n);
    return rc;
}
static int control_command(const char *command,char *out,int size)
{
    char verb[32],key[96],value[4001]; const char *rest; int i;
    rest=control_word(command,verb,sizeof(verb));
    for(i=0;verb[i];i++) if(verb[i]>='a'&&verb[i]<='z') verb[i]-=32;
    out[0]=0;
    if(G.transfer_dialog) {
        static const char *allowed[]={"HELP","CAPABILITIES","STATUS","DOCKSTATUS","SERVERSTATUS","SERVERS","ROOMS","GETCONFIG","DRAFT","PLUGINS","SUBSCRIBE","UNSUBSCRIBE","QUIT","SHOW",NULL};
        for(i=0;allowed[i] && strcmp(allowed[i],verb);i++);
        if(!allowed[i]) { snprintf(out,size,"BUSY transfer in progress"); return 10; }
        if(!strcmp(verb,"SHOW")) { if(G.transfer_win) { WindowToFront(G.transfer_win); ActivateWindow(G.transfer_win); } snprintf(out,size,"SHOWN"); return 0; }
    }
    if(!strcmp(verb,"HELP")) snprintf(out,size,"CAPABILITIES STATUS DOCKSTATUS SERVERSTATUS SERVERS SERVER ROOMS CONNECT DISCONNECT GETCONFIG SETCONFIG SAVE RELOAD DRAFT INSERT SEND OPENROOM NEWDIRECT NEWCHANNEL PLUGINS PLUGIN SUBSCRIBE UNSUBSCRIBE SHOW HIDE ICONIFY PREFERENCES QUIT");
    else if(!strcmp(verb,"CAPABILITIES")) snprintf(out,size,"protocol=2 notifications=1,2 snapshot=DOCKSTATUS rooms=ROOMS connection_control=1 unread_scope=all_subscriptions message_scope=last_opened_room_per_server notification_limit=16 subscriber_limit=8 per_subscriber_limit=2");
    else if(!strcmp(verb,"CONNECT") || !strcmp(verb,"DISCONNECT")) return control_connection(verb,rest,out,size);
    else if(!strcmp(verb,"ROOMS")) return control_rooms(rest,out,size);
    else if(!strcmp(verb,"SERVERSTATUS")) {
        int slot; const char *tail; char value[256];
        if(control_slot(rest,&slot,&tail) || *tail) return 10;
        dock_server_values(slot,value,sizeof(value)); snprintf(out,size,"%s error=%d",value,G.servers[slot].last_error);
    }
    else if(!strcmp(verb,"DOCKSTATUS")) {
        Buf b; char value[256]; buf_init(&b); control_summary(&b); buf_putc(&b,'\n');
        for(i=0;i<PREFS_SERVERS_MAX;i++) if(G.prefs.servers[i].url[0]) { dock_server_values(i,value,sizeof(value)); buf_printf(&b,"%s error=%d\n",value,G.servers[i].last_error); }
        return control_result(&b,out,size);
    }
    else if(!strcmp(verb,"SERVERS")) {
        Buf list; buf_init(&list);
        for(i=0;i<PREFS_SERVERS_MAX;i++) if(G.prefs.servers[i].url[0])
            buf_printf(&list,"%d\t%s\t%s\t%s\n",i+1,server_display(i),G.prefs.servers[i].url,
                dock_server_state(i));
        return control_result(&list,out,size);
    } else if(!strcmp(verb,"SERVER")) {
        char *end; long slot=strtol(rest,&end,10);
        if(!G.multi || !*rest || *end || slot<1 || slot>PREFS_SERVERS_MAX || !G.prefs.servers[slot-1].url[0]) return 10;
        server_select(slot-1); snprintf(out,size,"SERVER %d",G.active_server+1);
    }
    else if(!strcmp(verb,"STATUS")) {
        Buf b; buf_init(&b); control_summary(&b); buf_printf(&b," room=%s user=%s",G.cur_rid,G.username);
        return control_result(&b,out,size);
    } else if(!strcmp(verb,"GETCONFIG")) {
        control_word(rest,key,sizeof(key)); if(prefs_get(&G.prefs,key,out,size)<0) return 10;
    } else if(!strcmp(verb,"SETCONFIG")) {
        Prefs *next; rest=control_word(rest,key,sizeof(key)); control_value(rest,value,sizeof(value));
        if(!strcmp(key,"theme") && strcmp(theme_get(theme_find(value))->id,value)) return 10;
        next=ar_malloc(sizeof(*next)); if(!next) return 20; *next=G.prefs;
        i=prefs_set(next,key,value);
        if(!i) apply_preferences(next,0);
        memset(next,0,sizeof(*next)); ar_free(next); if(i<0) return 10;
        snprintf(out,size,"APPLIED SESSION");
    } else if(!strcmp(verb,"SAVE")) {
        if(prefs_save(&G.prefs)<0) return 20;
        G.session_override=0; prefs_service_publish(G.prefs_service,&G.prefs); prefs_runtime_send(&G.prefs,1); snprintf(out,size,"SAVED");
    } else if(!strcmp(verb,"RELOAD")) {
        Prefs *next=ar_malloc(sizeof(*next)); if(!next) return 20;
        i=prefs_load(next); if(!i) apply_preferences(next,1); ar_free(next); if(i<0) return 10; snprintf(out,size,"RELOADED");
    } else if(!strcmp(verb,"DRAFT")) { composer_capture(); ar_strlcpy(out,G.hook_buf,size); }
    else if(!strcmp(verb,"INSERT")) { control_value(rest,value,sizeof(value)); if(plugin_insert(NULL,value)<0) return 10; snprintf(out,size,"INSERTED"); }
    else if(!strcmp(verb,"SEND")) {
        if(!G.connected || !G.cur_rid[0]) return 10;
        if(!*rest) cmd_send_input();
        else {
            NetMsg *m; Buf b; control_value(rest,value,sizeof(value)); if(!*value || strlen(rest)>4002) return 10;
            m=netmsg_new(NC_SEND); if(!m) return 20; m->a_rid=ar_strdup(G.cur_rid);
            buf_init(&b); latin1_to_utf8(&b,value); m->a_text=buf_detach(&b);
            if(!m->a_rid||!m->a_text) { netmsg_free(m); return 20; } send_cmd(m);
        }
        snprintf(out,size,"QUEUED");
    } else if(!strcmp(verb,"OPENROOM")) {
        int slot=G.active_server; const RcRoomList *rooms; char *colon,*end; long number;
        control_value(rest,value,sizeof(value)); colon=strchr(value,':');
        if(colon) {
            number=strtol(value,&end,10);
            if(end==colon && end!=value) { if(number<1 || number>PREFS_SERVERS_MAX) return 10; slot=number-1; memmove(value,colon+1,strlen(colon+1)+1); }
        }
        if(!G.prefs.servers[slot].url[0] || strcmp(dock_server_state(slot),"connected")) return 10;
        rooms=dock_rooms(slot);
        for(i=0;i<rooms->count;i++) if(!strcmp(rooms->items[i].rid,value)||!strcmp(rooms->items[i].name,value)) break;
        if(i==rooms->count) return 10;
        ar_strlcpy(key,rooms->items[i].rid,sizeof(key));
        server_select(slot); if(G.active_server!=slot) return 20;
        G.server_view=0; ar_strlcpy(G.cur_rid,key,sizeof(G.cur_rid)); cmd_open_room(G.cur_rid); update_title(); rooms_rebuild(); snprintf(out,size,"QUEUED");
    } else if(!strcmp(verb,"NEWDIRECT")) {
        control_value(rest,value,sizeof(value)); i=control_create('d',"",value); if(i) return i; snprintf(out,size,"QUEUED");
    } else if(!strcmp(verb,"NEWCHANNEL")) {
        char type[16],name[129]; rest=control_word(rest,type,sizeof(type)); rest=control_word(rest,name,sizeof(name));
        if(strcmp(type,"public")&&strcmp(type,"private")) return 10;
        i=control_create(!strcmp(type,"private")?'p':'c',name,rest); if(i) return i; snprintf(out,size,"QUEUED");
    } else if(!strcmp(verb,"PLUGINS")) {
        Buf b; buf_init(&b); for(i=0;i<plugins_count();i++) { if(i) buf_putc(&b,','); buf_puts(&b,plugins_id(i)); }
        ar_strlcpy(out,buf_str(&b),size); buf_free(&b);
    } else if(!strcmp(verb,"PLUGIN")) {
        control_word(rest,key,sizeof(key)); for(i=0;i<plugins_count() && strcmp(key,plugins_id(i));i++);
        if(i==plugins_count()) return 10;
        plugins_action(i); snprintf(out,size,"DONE");
    } else if(!strcmp(verb,"SUBSCRIBE")) { const char *tail=control_word(rest,key,sizeof(key)); int version=1;
        if(*tail) { if(!strcmp(tail,"2")) version=2; else return 10; }
        if(!arexx_subscribe_version(key,version)) return 10;
        snprintf(out,size,"SUBSCRIBED version=%d epoch=%s seq=%lu",version,arexx_epoch(),(unsigned long)arexx_sequence()); }
    else if(!strcmp(verb,"UNSUBSCRIBE")) { control_word(rest,key,sizeof(key)); arexx_unsubscribe(key); snprintf(out,size,"UNSUBSCRIBED"); }
    else if(!strcmp(verb,"ICONIFY")) { if(!main_iconify()) return 20; snprintf(out,size,"ICONIFIED"); }
    else if(!strcmp(verb,"SHOW")) { if(G.iconified && !main_restore()) return 20; if(G.scr) ScreenToFront(G.scr); if(G.win) { WindowToFront(G.win); ActivateWindow(G.win); } }
    else if(!strcmp(verb,"HIDE")) { if(G.win) WindowToBack(G.win); if(G.own_screen) ScreenToBack(G.scr); }
    else if(!strcmp(verb,"PREFERENCES")) launch_preferences();
    else if(!strcmp(verb,"QUIT")) G.quit=1;
    else return 10;
    return 0;
}

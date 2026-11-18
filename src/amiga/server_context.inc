/* A single view borrows the selected server's data. Other servers retain their
 * own rooms, history and draft; every network packet carries slot + generation. */
static void login_close(void);
static void login_open(const char *hint);
static void went_offline(const char *why);
static void application_event(const char *,const char *,const char *,const char *);
static void server_message_event(int,const char *,const char *,const char *);
static void notify_unread(void);
static void server_send(int slot,NetMsg *m)
{
    if(!m) return;
    m->server_slot=slot; m->server_generation=G.servers[slot].generation;
    G.pending++; worker_send(m);
}
static void server_state_free(ServerState *s)
{
    int generation=s->generation+1;
    rc_roomlist_free(&s->rooms); rc_msglist_free(&s->msgs); rc_msglist_free(&s->events);
    ar_free(s->members); ar_free(s->presence); ar_free(s->draft);
    memset(s,0,sizeof(*s)); s->generation=generation; s->expanded=1;
}
#define SERVER_SCALARS(X) X(connected) X(busy) X(history_pending) X(history_exhausted) X(send_pending) X(room_serial) X(download_pending) X(tz_ms) X(tz_known) X(anchor_line) X(read_boundary) X(boundary_unread)
#define SERVER_ARRAYS(X) X(cur_rid) X(loading_rid) X(topic) X(username) X(host) X(anchor_id) X(presence_user_id) X(status) X(unread_anchor) X(marker_room)
static void server_notice(int slot,const char *text)
{
    ServerState *s; RcMsgList one; RcMsg *event; char id[48];
    if(!G.multi || slot<0 || slot>=PREFS_SERVERS_MAX || !text || !*text) return;
    s=&G.servers[slot];
    rc_msglist_init(&one); one.items=ar_calloc(1,sizeof(RcMsg)); if(!one.items) return;
    one.count=one.cap=1; event=one.items;
    snprintf(id,sizeof(id),"server-%d-event-%d",slot,++s->event_serial);
    event->id=ar_strdup(id); event->rid=ar_strdup("__server_events__"); event->username=ar_strdup("Server");
    event->text=ar_strdup(text); event->ts_ms=amiga_local_ms()-G.tz_ms; if(s->events.count && event->ts_ms<=s->events.items[s->events.count-1].ts_ms) event->ts_ms=s->events.items[s->events.count-1].ts_ms+1;
    event->updated_ms=event->ts_ms;
    if(event->id && event->rid && event->username && event->text) rc_msglist_merge(&s->events,&one,128);
    rc_msglist_free(&one);
    if(slot==G.active_server && G.server_view) msgs_rebuild(1);
    mark_dirty(DIRTY_SIDE|DIRTY_HEADER);
}
static void server_event(int slot,int error,const char *text)
{
    ServerState *s;
    if(!G.multi || slot<0 || slot>=PREFS_SERVERS_MAX || !text || !*text) return;
    s=&G.servers[slot];
    if(s->last_error==error && !strcmp(s->connection_text,text)) return;
    s->last_error=error;
    if(!error) s->reconnecting=0;
    ar_strlcpy(s->connection_text,text,sizeof(s->connection_text));
    { char id[16],code[24]; snprintf(id,sizeof(id),"%d",slot+1); snprintf(code,sizeof(code),"%d",error);
      application_event("SERVERSTATUS",id,code,text); }
    server_notice(slot,text);
}
static int server_stash(void)
{
    ServerState *s=&G.servers[G.active_server]; STRPTR text=NULL; char *draft;
    server_legacy_save();
    if((G.win || G.iconified) && G.g[G_INPUT]) {
        composer_capture();
        text=(STRPTR)DoGadgetMethod((struct Gadget *)G.g[G_INPUT],G.win,NULL,GM_TEXTEDITOR_ExportText,NULL);
    }
    draft=ar_strdup(text?(const char *)text:G.hook_buf);
    if(text) FreeVec(text);
    if(!draft) { set_status("Not enough memory to switch servers; draft kept"); return 0; }
    ar_free(s->draft); s->draft=draft; s->cursor=G.hook_pos;
#define SAVE_SCALAR(x) s->x=G.x;
#define SAVE_ARRAY(x) memcpy(s->x,G.x,sizeof(s->x));
    SERVER_SCALARS(SAVE_SCALAR) SERVER_ARRAYS(SAVE_ARRAY)
#undef SAVE_SCALAR
#undef SAVE_ARRAY
    s->chat_top=G.chat.top; s->chat_follow=G.chat.follow;
    s->rooms=G.rooms; rc_roomlist_init(&G.rooms); s->msgs=G.msgs; rc_msglist_init(&G.msgs);
    s->members=G.members; G.members=NULL; s->presence=G.presence; G.presence=NULL; return 1;
}
static void server_load(int slot)
{
    ServerState *s=&G.servers[slot]; G.active_server=slot; server_legacy_load(slot);
#define LOAD_SCALAR(x) G.x=s->x;
#define LOAD_ARRAY(x) memcpy(G.x,s->x,sizeof(G.x));
    SERVER_SCALARS(LOAD_SCALAR) SERVER_ARRAYS(LOAD_ARRAY)
#undef LOAD_SCALAR
#undef LOAD_ARRAY
    G.rooms=s->rooms; rc_roomlist_init(&s->rooms); G.msgs=s->msgs; rc_msglist_init(&s->msgs);
    G.members=s->members; s->members=NULL; G.presence=s->presence; s->presence=NULL;
    G.chat.top=s->chat_top; G.chat.follow=s->chat_follow;
    if((G.win || G.iconified) && G.g[G_INPUT]) composer_set(s->draft?s->draft:"",s->cursor);
    ar_free(s->draft); s->draft=NULL;
    if(!G.status[0]) ar_strlcpy(G.status,"Disconnected - choose Project / Connect",sizeof(G.status));
    G.copy_chat=0; G.n_sugg=0; G.last_unread=-1; G.server_view=1;
}
static void server_select(int slot)
{
    if(slot<0 || slot>=PREFS_SERVERS_MAX || !G.prefs.servers[slot].url[0] || slot==G.active_server) return;
    if(!server_stash()) return;
    if(G.running_client) { NetMsg *focus=netmsg_new(NC_SET_FOCUS); if(focus) { focus->a_interval=0; server_send(G.active_server,focus); } }
    login_close(); avatar_reset(); images_reset(); server_load(slot); /* Preserve the saved expansion state. */
    G.sugg_move=G.editor_action=0;
    if(G.running_client) { NetMsg *focus=netmsg_new(NC_SET_FOCUS); if(focus) { focus->a_interval=0; server_send(slot,focus); } }
    rooms_rebuild(); msgs_rebuild(0); update_title(); set_busy(G.busy); set_status(G.status);
    if(G.running_client && G.connected && G.cur_rid[0] && !G.msgs.count) cmd_open_room(G.cur_rid);
    if(G.running_client && G.connected && !G.cur_rid[0] && G.rooms.count) {
        ar_strlcpy(G.cur_rid,G.rooms.items[0].rid,sizeof(G.cur_rid)); cmd_open_room(G.cur_rid);
    }
}
static int server_connect(int slot,int interactive)
{
    ServerPrefs *p=&G.prefs.servers[slot]; ServerState *s=&G.servers[slot]; NetMsg *m;
    if(!p->url[0]) return 10;
    if(slot==G.active_server?G.busy:s->busy) return 0;
    if(!p->remember || !p->user_id[0] || !p->token[0]) {
        ar_strlcpy(s->status,"Login required - select server and choose Connect",sizeof(s->status));
        server_event(slot,RC_ERR_AUTH,"Login required"); notify_unread();
        if(interactive && slot==G.active_server) { login_open(NULL); return 0; }
        return 10;
    }
    m=netmsg_new(NC_CONNECT); if(!m) return 20;
    m->a_url=ar_strdup(p->url); m->a_uid=ar_strdup(p->user_id);
    m->a_token=ar_strdup(p->token); m->a_verify=p->verify;
    if(!m->a_url || !m->a_uid || !m->a_token) { netmsg_free(m); return 20; }
    server_event(slot,0,"Connecting with saved login...");
    s->busy=1; if(slot==G.active_server) { set_busy(1); set_status("Connecting with saved login..."); }
    server_send(slot,m); rooms_rebuild(); notify_unread(); return 0;
}
static void servers_init(void)
{
    int i,first=-1;
    prefs_migrate_servers(&G.prefs); G.multi=1;
    for(i=0;i<PREFS_SERVERS_MAX;i++) {
        char key[64]; int mask,j;
        G.servers[i].generation=1; G.servers[i].chat_follow=1;
        snprintf(key,sizeof(key),"workspace.%d.tree",i); mask=atoi(prefs_extra(&G.prefs,key,"1"));
        G.servers[i].expanded=mask&1;
        for(j=0;j<SEC_N;j++) G.servers[i].collapsed[j]=(mask>>(j+1))&1;
        snprintf(key,sizeof(key),"workspace.%d.room",i); ar_strlcpy(G.servers[i].cur_rid,prefs_extra(&G.prefs,key,""),sizeof(G.servers[i].cur_rid));
        if(first<0 && G.prefs.servers[i].url[0]) first=i;
    }
    { int saved=atoi(prefs_extra(&G.prefs,"workspace.server","-1"));
      G.active_server=saved>=0 && saved<PREFS_SERVERS_MAX && G.prefs.servers[saved].url[0]?saved:first<0?0:first; }
    G.server_view=atoi(prefs_extra(&G.prefs,"workspace.events","1")); server_legacy_load(G.active_server);
    ar_strlcpy(G.cur_rid,G.servers[G.active_server].cur_rid,sizeof(G.cur_rid));
}
static int server_background(NetMsg *m)
{
    int slot=m->server_slot,i; ServerState *s=&G.servers[slot]; ServerPrefs *p=&G.prefs.servers[slot];
    switch(m->kind) {
    case NE_RECONNECTED: case NC_CONNECT:
        server_event(slot,m->rc,m->rc==RC_OK?"Connected":m->text[0]?m->text:rc_strerror(m->rc));
        s->busy=0; s->connected=m->rc==RC_OK; if(m->rc==RC_OK) s->reconnecting=0;
        if(m->rc==RC_OK) {
            rc_roomlist_free(&s->rooms); s->rooms=m->rooms; rc_roomlist_init(&m->rooms);
            ar_strlcpy(s->username,m->username,sizeof(s->username)); ar_strlcpy(s->presence_user_id,m->user_id,sizeof(s->presence_user_id));
            if(p->remember) { ar_strlcpy(p->user_id,m->user_id,sizeof(p->user_id)); ar_strlcpy(p->token,m->token,sizeof(p->token)); }
            ar_strlcpy(s->status,"Connected",sizeof(s->status)); prefs_save_client_state();
        } else {
            ar_strlcpy(s->status,m->rc==RC_ERR_TOTP?"Two-factor login required":m->text,sizeof(s->status));
            if(m->rc==RC_ERR_AUTH) { p->token[0]=p->user_id[0]=0; prefs_save_client_state(); }
        }
        break;
    case NC_CREATE_ROOM:
        if(m->rc==RC_OK) rc_roomlist_merge(&s->rooms,&m->rooms);
        break;
    case NC_OPEN_ROOM:
        if(strcmp(s->cur_rid,m->rid) || s->room_serial!=m->a_interval) break;
        s->loading_rid[0]=0;
        if(m->rc==RC_OK) {
            rc_msglist_free(&s->msgs); rc_msglist_merge(&s->msgs,&m->msgs,MAX_MESSAGES);
            s->history_exhausted=s->msgs.count<50;
            ar_strlcpy(s->topic,m->topic,sizeof(s->topic)); ar_free(s->members); s->members=m->members; m->members=NULL;
        } break;
    case NC_HISTORY:
        s->history_pending=0;
        if(m->rc==RC_OK && !strcmp(s->cur_rid,m->rid)) { s->history_exhausted=m->msgs.count<50; rc_msglist_merge(&s->msgs,&m->msgs,MAX_MESSAGES); }
        break;
    case NC_SEND:
        s->send_pending=0;
        if(m->rc==RC_OK) {
            Buf utf8; buf_init(&utf8); latin1_to_utf8(&utf8,s->draft?s->draft:"");
            if(m->a_text && !strcmp(s->cur_rid,m->rid) && !strcmp(ar_trim(utf8.data?utf8.data:(char *)""),m->a_text)) { ar_free(s->draft); s->draft=NULL; s->cursor=0; }
            buf_free(&utf8);
            if(!strcmp(s->cur_rid,m->rid)) rc_msglist_merge(&s->msgs,&m->msgs,MAX_MESSAGES);
        } else {
            ar_strlcpy(s->status,m->text,sizeof(s->status));
            server_notice(slot,m->text[0]?m->text:rc_strerror(m->rc));
            char qualified[96]; snprintf(qualified,sizeof(qualified),"%d:%s",slot+1,m->rid);
            application_event("SENDFAILED",qualified,s->username,m->text);
        }
        break;
    case NE_UPDATE:
        dock_room_delta(slot,&m->rooms);
        for(i=0;i<m->msgs.count;i++) {
            int k; for(k=0;k<s->msgs.count;k++) if(!strcmp(m->msgs.items[i].id,s->msgs.items[k].id)) break;
            if(k==s->msgs.count) server_message_event(slot,m->rid,m->msgs.items[i].username,m->msgs.items[i].text);
        }
        for(i=0;i<m->rooms.n_removed;i++) if(!strcmp(s->cur_rid,m->rooms.removed[i])) { s->cur_rid[0]=0; rc_msglist_free(&s->msgs); }
        rc_roomlist_merge(&s->rooms,&m->rooms);
        if(!strcmp(s->cur_rid,m->rid)) rc_msglist_merge(&s->msgs,&m->msgs,MAX_MESSAGES);
        break;
    case NC_PRESENCE: case NE_PRESENCE:
        if(m->rc==RC_OK) { ar_free(s->presence); s->presence=m->members; m->members=NULL; } break;
    case NC_LOGOUT: case NC_DISCONNECT:
        server_event(slot,0,m->kind==NC_LOGOUT?"Logged out":"Disconnected");
        s->connected=s->busy=0; rc_roomlist_free(&s->rooms); rc_msglist_free(&s->msgs); s->cur_rid[0]=0; break;
    case NE_STATUS:
        server_event(slot,m->rc,m->text); s->reconnecting=m->a_interval>0;
        ar_strlcpy(s->status,m->text,sizeof(s->status));
        if(m->rc==RC_ERR_AUTH) { s->connected=0; s->reconnecting=0; p->token[0]=p->user_id[0]=0; prefs_save_client_state(); }
        break;
    case NC_DOWNLOAD: s->download_pending=0; break;
    }
    rooms_rebuild(); notify_unread(); return 1;
}
static void server_reconfigure(const Prefs *next)
{
    int i; server_legacy_save();
    for(i=0;i<PREFS_SERVERS_MAX;i++) {
        ServerPrefs value=next->servers[i],*old=&G.prefs.servers[i];
        int changed=strcmp(value.url,old->url)||strcmp(value.user,old->user)||value.verify!=old->verify;
        if(changed) {
            NetMsg *m;
            if(i==G.active_server) {
                login_close(); avatar_reset(); images_reset();
                rc_roomlist_free(&G.rooms); rc_msglist_free(&G.msgs); ar_free(G.members); G.members=NULL;
                G.connected=G.busy=G.send_pending=G.history_pending=G.download_pending=0; G.server_view=1;
                G.cur_rid[0]=G.loading_rid[0]=G.topic[0]=G.username[0]=G.presence_user_id[0]=0;
                if(G.win || G.iconified) composer_set("",0);
            }
            server_state_free(&G.servers[i]);
            m=netmsg_new(NC_DISCONNECT); server_send(i,m);
        } else if(value.remember) {
            ar_strlcpy(value.token,old->token,sizeof(value.token)); ar_strlcpy(value.user_id,old->user_id,sizeof(value.user_id));
        }
        if(!value.remember) value.token[0]=value.user_id[0]=0;
        *old=value;
    }
    G.prefs.servers_defined=next->servers_defined; server_legacy_load(G.active_server);
    if(!G.prefs.server[0]) for(i=0;i<PREFS_SERVERS_MAX;i++) if(G.prefs.servers[i].url[0]) { server_select(i); break; }
    rooms_rebuild(); update_title(); msgs_rebuild(0);
    notify_unread(); application_event("SERVERS","","","changed");
}
static void servers_cleanup(void)
{
    int i; for(i=0;i<PREFS_SERVERS_MAX;i++) server_state_free(&G.servers[i]);
}

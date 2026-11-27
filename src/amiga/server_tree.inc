static const char *server_display(int slot)
{
    static char label[256]; ServerUrl url; char error[80]; const ServerPrefs *p=&G.prefs.servers[slot];
    if(p->name[0]) return p->name;
    if(!url_parse_server(p->url,&url,error,sizeof(error))) {
        if(url.port!=(url.tls?443:80)) snprintf(label,sizeof(label),"%s:%d",url.host,url.port);
        else ar_strlcpy(label,url.host,sizeof(label));
    } else ar_strlcpy(label,p->url,sizeof(label));
    return label;
}
static SRow *tree_section(int slot,int section,int depth)
{
    SRow *row=side_add(&G.side); if(!row) return NULL;
    row->kind=SR_SECTION; row->section=section; row->server_slot=slot; row->depth=depth;
    row->collapsed=G.servers[slot].collapsed[section]; ar_strlcpy(row->label,g_sec_names[section],sizeof(row->label)); return row;
}
static void tree_rooms(int slot,int section,int depth,const RcRoomList *rooms,const char *rid)
{
    int i,n=0,*order=ar_malloc((rooms->count+1)*sizeof(int)); Buf b; ServerState *s=&G.servers[slot];
    if(!order) return;
    buf_init(&b);
    for(i=0;i<rooms->count;i++) {
        const RcRoom *r=&rooms->items[i];
        if((r->open || !strcmp(r->rid,rid)) && room_section(r)==section && room_matches(r)) order[n++]=i;
    }
    g_sort_rooms=rooms; qsort(order,n,sizeof(int),cmp_room_idx);
    if(!s->collapsed[section] || G.filter[0]) for(i=0;i<n;i++) {
        const RcRoom *r=&rooms->items[order[i]]; SRow *row=side_add(&G.side); const char *label;
        if(!row) break;
        row->kind=SR_ROOM; row->section=section; row->server_slot=slot; row->depth=depth; row->rid=r->rid;
        row->glyph=r->type=='d'?'@':r->type=='p'?'*':'#'; row->is_dm=r->type=='d';
        row->participants=rc_dm_participants(r,G.username);
        if(row->is_dm && !row->participants) ar_strlcpy(row->avatar_user,r->name,sizeof(row->avatar_user));
        row->selected=slot==G.active_server && !G.server_view && !strcmp(r->rid,rid);
        row->mentions=r->mentions>999?999:r->mentions; row->bold=r->unread>0 || r->alert || r->mentions; row->count=r->unread>999?999:r->unread>0?r->unread:r->alert?-1:0;
        buf_clear(&b); room_label(&b,r,0); label=buf_str(&b); if(b.len>2) label+=2;
        ar_strlcpy(row->label,label,sizeof(row->label));
    }
    buf_free(&b); ar_free(order);
}
static void tree_rebuild(void)
{
    int slot; char user[96]; ar_strlcpy(user,G.username,sizeof(user)); side_clear(&G.side);
    for(slot=0;slot<PREFS_SERVERS_MAX;slot++) if(G.prefs.servers[slot].url[0]) {
        ServerState *s=&G.servers[slot]; SRow *row=side_add(&G.side); int i,unread=0,mentions=0;
        int connected=(slot==G.active_server?G.connected:s->connected) && (!s->last_error || s->cooling || s->health_retry), busy=slot==G.active_server?G.busy:s->busy;
        if(s->last_error && !s->cooling && !s->health_retry) connected=0;
        const RcRoomList *rooms=slot==G.active_server?&G.rooms:&s->rooms;
        const char *rid=slot==G.active_server?G.cur_rid:s->cur_rid;
        if(!row) break;
        for(i=0;i<rooms->count;i++) { int n=rooms->items[i].unread;
            if(n>0) unread=n>2147483647-unread?2147483647:unread+n;
            if(rooms->items[i].mentions>0) mentions=1; }
        row->kind=SR_SERVER; row->server_slot=slot; row->collapsed=!s->expanded;
        row->selected=slot==G.active_server && G.server_view; row->bold=unread>0 || mentions;
        row->connection_state=(s->cooling || s->health_retry || s->reconnecting)?1:s->last_error?3:connected?2:busy?1:0;
        { char health[96]; snprintf(row->label,sizeof(row->label),"%s - %s",server_display(slot),server_health(slot,health,sizeof(health))); }
        if(unread>0) { size_t n=strlen(row->label); snprintf(row->label+n,sizeof(row->label)-n," (%d)",unread); }
        if(!connected) continue;
        if(!s->expanded && !G.filter[0]) continue;
        ar_strlcpy(G.username,slot==G.active_server?user:s->username,sizeof(G.username));
        tree_section(slot,SEC_ROOMS,1);
        if(!s->collapsed[SEC_ROOMS] || G.filter[0]) {
            tree_section(slot,SEC_FAV,2); tree_rooms(slot,SEC_FAV,3,rooms,rid);
            tree_section(slot,SEC_CHAN,2); tree_rooms(slot,SEC_CHAN,3,rooms,rid);
        }
        tree_section(slot,SEC_DM,1); tree_rooms(slot,SEC_DM,2,rooms,rid);
    }
    ar_strlcpy(G.username,user,sizeof(G.username)); mark_dirty(DIRTY_SIDE|DIRTY_HEADER);
}

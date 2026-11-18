/* Pure dock snapshots: no server selection, read receipts or draft changes. */
static const RcRoomList *dock_rooms(int slot)
{
    return slot==G.active_server?&G.rooms:&G.servers[slot].rooms;
}
static int dock_unread(int slot)
{
    const RcRoomList *r=dock_rooms(slot); int i,total=0;
    for(i=0;i<r->count;i++) if(r->items[i].unread>0) {
        int n=r->items[i].unread; if(n>2147483647-total) return 2147483647; total+=n;
    }
    return total;
}
static int dock_alerts(int slot)
{
    const RcRoomList *r=dock_rooms(slot); int i,total=0;
    for(i=0;i<r->count;i++) if(r->items[i].alert) total++;
    return total;
}
static const char *dock_server_state(int slot)
{
    const ServerState *s=&G.servers[slot]; int busy=slot==G.active_server?G.busy:s->busy;
    int connected=slot==G.active_server?G.connected:s->connected;
    if(s->reconnecting) return "reconnecting";
    if(busy) return "connecting";
    if(s->last_error) return "failed";
    return connected?"connected":"offline";
}
static void dock_server_values(int slot,char *out,int size)
{
    const ServerPrefs *p=&G.prefs.servers[slot]; const char *rid=slot==G.active_server?G.cur_rid:G.servers[slot].cur_rid;
    snprintf(out,size,"server=%d state=%s unread=%d alerts=%d generation=%d login_required=%d room=%s",
        slot+1,dock_server_state(slot),dock_unread(slot),dock_alerts(slot),G.servers[slot].generation,
        !p->remember || !p->user_id[0] || !p->token[0],rid);
}
static int dock_total_unread(void)
{
    int slot,total=0;
    if(!G.multi) return dock_unread(G.active_server);
    for(slot=0;slot<PREFS_SERVERS_MAX;slot++) if(G.prefs.servers[slot].url[0]) {
        int n=dock_unread(slot); if(n>2147483647-total) return 2147483647; total+=n;
    }
    return total;
}
static void notify_dock_state(void)
{
    int slot; char id[16],value[256];
    if(!G.multi) return;
    for(slot=0;slot<PREFS_SERVERS_MAX;slot++) if(G.prefs.servers[slot].url[0]) {
        dock_server_values(slot,value,sizeof(value));
        if(strcmp(value,G.servers[slot].dock_snapshot)) {
            ar_strlcpy(G.servers[slot].dock_snapshot,value,sizeof(G.servers[slot].dock_snapshot));
            snprintf(id,sizeof(id),"%d",slot+1);
            application_event("SERVERSTATE",id,"",value);
        }
    }
}
/* Subscription deltas cover every joined room, even without message previews. */
static void dock_room_delta(int slot,const RcRoomList *delta)
{
    const RcRoomList *old=dock_rooms(slot); int i,k; char id[96],value[80];
    for(i=0;i<delta->count;i++) {
        const RcRoom *r=&delta->items[i]; int unread=r->unread,alert=r->alert;
        if(slot==G.active_server && conversation_visible() && !strcmp(r->rid,G.cur_rid)) unread=alert=0;
        for(k=0;k<old->count;k++) if(!strcmp(old->items[k].rid,r->rid)) break;
        if(k<old->count && old->items[k].unread==unread && old->items[k].alert==alert) continue;
        snprintf(id,sizeof(id),"%d:%s",slot+1,r->rid);
        snprintf(value,sizeof(value),"unread=%d alert=%d",unread>0?unread:0,!!alert);
        application_event("ROOMUNREAD",id,"",value);
    }
    for(i=0;i<delta->n_removed;i++) {
        snprintf(id,sizeof(id),"%d:%s",slot+1,delta->removed[i]);
        application_event("ROOMUNREAD",id,"","unread=0 alert=0 removed=1");
    }
}

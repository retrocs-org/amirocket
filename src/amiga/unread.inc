/* Read boundary is captured before focus/read receipts clear subscription counts. */
static void unread_capture(const char *rid)
{
    const RcRoom *r=find_room(rid);
    if(strcmp(G.marker_room,rid) || (r && (r->unread>0 || r->alert || r->mentions))) {
        ar_strlcpy(G.marker_room,rid,sizeof(G.marker_room)); G.unread_anchor[0]=0;
        G.read_boundary=r?r->last_seen_ms:0;
        G.boundary_unread=r?(r->unread>0?r->unread:r->alert||r->mentions?1:0):0;
    }
}
static void unread_resolve(void)
{
    int i,first=-1,left=G.boundary_unread;
    if(!left || strcmp(G.marker_room,G.cur_rid)) return;
    if(G.read_boundary>0) {
        for(i=0;i<G.msgs.count;i++) if(G.msgs.items[i].ts_ms>G.read_boundary) { first=i; break; }
    } else {
        /* Older servers may omit ls. Use their unread count as a fallback. */
        for(i=G.msgs.count-1;i>=0;i--) if(strcmp(G.msgs.items[i].username,G.username)) { first=i; if(--left<=0) break; }
    }
    if(first>=0) {
        ar_strlcpy(G.unread_anchor,G.msgs.items[first].id,sizeof(G.unread_anchor));
        if(!G.read_boundary && left<=0 && G.msgs.items[first].ts_ms>0) G.read_boundary=G.msgs.items[first].ts_ms-1;
    } else G.unread_anchor[0]=0;
}
static int unread_rank(int slot,const RcRoom *a,int other,const RcRoom *b)
{
    int d=slot-other; if(d) return d;
    d=room_section(a)-room_section(b); return d?d:rc_room_compare(a,b);
}
/* Independent of filters/collapse state; wraps once through connected servers. */
static int unread_target(int *slot_out,char *rid,int size)
{
    int slot,i,first_slot=-1,next_slot=-1; const RcRoom *first=NULL,*next=NULL,*current=find_room(G.cur_rid);
    for(slot=0;slot<(G.multi?PREFS_SERVERS_MAX:1);slot++) {
        const RcRoomList *rooms=G.multi?dock_rooms(slot):&G.rooms;
        if(G.multi ? !G.prefs.servers[slot].url[0] || strcmp(dock_server_state(slot),"connected") : !G.connected) continue;
        for(i=0;i<rooms->count;i++) {
            const RcRoom *r=&rooms->items[i]; int after;
            if(!r->open || !(r->unread>0 || r->alert || r->mentions)) continue;
            if(!G.server_view && slot==G.active_server && !strcmp(r->rid,G.cur_rid)) continue;
            if(!first || unread_rank(slot,r,first_slot,first)<0) { first=r; first_slot=slot; }
            after=slot>G.active_server || (slot==G.active_server && (G.server_view || !current || unread_rank(slot,r,slot,current)>0));
            if(after && (!next || unread_rank(slot,r,next_slot,next)<0)) { next=r; next_slot=slot; }
        }
    }
    if(!next) { next=first; next_slot=first_slot; }
    if(!next) return 0;
    *slot_out=next_slot; ar_strlcpy(rid,next->rid,size); return 1;
}
static void next_unread(void)
{
    int slot,section,i,already_loading; char rid[64];
    if(!unread_target(&slot,rid,sizeof(rid))) { request("Unread rooms","No unread rooms on connected servers."); return; }
    if(slot!=G.active_server) server_select(slot);
    if(slot!=G.active_server) return;
    G.server_view=0; G.copy_chat=0; G.n_sugg=0; G.filter[0]=0;
    if(G.g[G_FILTER] && G.win) SetGadgetAttrs((struct Gadget *)G.g[G_FILTER],G.win,NULL,STRINGA_TextVal,(ULONG)"",TAG_END);
    already_loading=!strcmp(G.loading_rid,rid);
    section=room_section(find_room(rid));
    if(G.multi) { G.servers[slot].expanded=1; G.servers[slot].collapsed[SEC_ROOMS]=0; G.servers[slot].collapsed[section]=0; }
    else G.collapsed[section]=0;
    ar_strlcpy(G.cur_rid,rid,sizeof(G.cur_rid)); G.topic[0]=0;
    rc_msglist_free(&G.msgs); ar_free(G.members); G.members=NULL;
    if(!already_loading) cmd_open_room(G.cur_rid);
    rooms_rebuild(); msgs_rebuild(1); update_title();
    for(i=0;i<G.side.n;i++) if(G.side.r[i].server_slot==slot && G.side.r[i].rid && !strcmp(G.side.r[i].rid,rid)) { scroll_side(i); break; }
    if(G.win) ActivateLayoutGadget((struct Gadget *)G.g[0],G.win,NULL,(ULONG)G.g[G_INPUT]);
}
static int unread_needs_history(void)
{
    int i,available=0;
    if(!G.unread_seek || !G.boundary_unread || !G.connected || !G.msgs.count || G.history_exhausted || G.msgs.count>=MAX_MESSAGES)return 0;
    if(G.read_boundary>0)return G.msgs.items[0].ts_ms>G.read_boundary;
    for(i=0;i<G.msgs.count;i++)if(strcmp(G.msgs.items[i].username,G.username))available++;
    return available<G.boundary_unread;
}
static void jump_unread(void)
{
    int i; if(G.server_view) return;
    if(unread_needs_history()){
        G.unread_seek=1;load_older();set_status("Finding first unread message... Escape stops history loading");return;
    }
    G.unread_seek=0;
    if(G.boundary_unread && G.read_boundary>0 && G.msgs.count>=MAX_MESSAGES && G.msgs.items[0].ts_ms>G.read_boundary)set_status("Retention limit: showing the earliest loaded unread message");
    redraw_dirty();
    for(i=0;i<G.chat.n;i++) if(G.chat.v[i].kind==VL_UNREAD) { chat_scroll_to(&G.chat,i);draw_chat();return; }
    request("Unread messages","No new-message marker in the loaded history.");
}

static void seek_first_unread(void)
{
    if(G.server_view)return;
    G.unread_seek=1;jump_unread();
}

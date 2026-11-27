/* Run inside the disposable emulator. Never uses real accounts or servers. */
static void qa_servers(int show)
{
    Prefs *saved=ar_malloc(sizeof(Prefs)),*next=ar_malloc(sizeof(Prefs)),*loaded=ar_malloc(sizeof(Prefs));
    NetMsg *m; int i,roots=0,fav=0,other=0,dm=0,old_pending=G.pending; char err[128];
    qa_trace("QA multiple server profiles and migration\n");
    QA(saved && next && loaded); if(!saved||!next||!loaded) goto cleanup;
    *saved=G.prefs; prefs_defaults(next); strcpy(next->server,"https://chat.one.test"); strcpy(next->user,"you");
    strcpy(next->user_id,"same-user-id"); strcpy(next->token,"first-token"); prefs_migrate_servers(next);
    QA(next->servers_defined && next->servers[0].autoconnect && !strcmp(next->servers[0].token,"first-token"));
    next->servers[1]=next->servers[0]; strcpy(next->servers[1].url,"https://chat.two.test"); strcpy(next->servers[1].token,"second-token"); next->servers[1].autoconnect=0;
    QA(prefs_server_validate(&next->servers[1],err,sizeof(err))==0);
    next->servers[1].quiet=1; next->servers[1].notify_mentions=1; next->servers[1].sound=0;
    QA(prefs_save(next)==0 && prefs_load(loaded)==0);
    QA(loaded->servers[1].quiet && loaded->servers[1].notify_mentions && !loaded->servers[1].sound);
    QA(loaded->servers[0].autoconnect && !loaded->servers[1].autoconnect && !strcmp(loaded->servers[1].token,"second-token"));
    loaded->servers[1].remember=0; QA(prefs_save(loaded)==0 && prefs_load(loaded)==0 && !loaded->servers[1].token[0]);
    G.prefs=*next; servers_init(); G.connected=1; G.prefs.show_avatars=0; strcpy(G.username,"you");
    strcpy(G.cur_rid,"r1"); G.rooms.items[0].favorite=1;
    rc_roomlist_copy(&G.servers[1].rooms,&G.rooms); G.servers[1].connected=1;
    strcpy(G.servers[1].cur_rid,"r1"); G.servers[1].room_serial=99; strcpy(G.servers[1].username,"you");
    G.servers[1].draft=ar_strdup("draft for second server");
    composer_set("draft for first server",5); rooms_rebuild();
    for(i=0;i<G.side.n;i++) {
        SRow *r=&G.side.r[i];
        if(r->kind==SR_SERVER) roots++;
        if(r->kind==SR_SECTION && r->section==SEC_FAV && r->depth==2) fav++;
        if(r->kind==SR_SECTION && r->section==SEC_CHAN && r->depth==2) other++;
        if(r->kind==SR_SECTION && r->section==SEC_DM && r->depth==1) dm++;
    }
    QA(roots==2 && fav==2 && other==2 && dm==2);
    qa_trace("QA unread visibility and workspace settings\n");
    G.rooms.items[0].unread=7; server_select(1); server_select(0);
    QA(G.rooms.items[0].unread==7 && G.server_view && !conversation_visible());
    G.server_view=0; QA(conversation_visible()==!!(G.win->Flags&WFLG_WINDOWACTIVE));
    G.server_view=1;
    QA(prefs_set(&G.prefs,"workspace.sidebar","42")==0 && sidebar_weight()==42);
    QA(prefs_set(&G.prefs,"workspace.0.tree","5")==0);
    { Prefs *compare=ar_malloc(sizeof(Prefs)); QA(compare!=NULL); if(compare) {
        *compare=G.prefs; QA(prefs_ui_equal(compare,&G.prefs)); compare->show_emoji=!compare->show_emoji; QA(!prefs_ui_equal(compare,&G.prefs)); ar_free(compare);
    } }

    qa_trace("QA aggregate dock snapshots and server-qualified controls\n");
    { char reply[4096]; int selected=G.active_server; unsigned long unread=0;
      G.rooms.items[0].unread=7; G.servers[1].rooms.items[0].unread=5;
      QA(control_command("STATUS",reply,sizeof(reply))==0 && ar_rexx_number(reply,"unread",&unread) && unread==12);
      QA(control_command("DOCKSTATUS",reply,sizeof(reply))==0 && strstr(reply,"server=1 state=connected unread=7") && strstr(reply,"server=2 state=connected unread=5"));
      QA(G.active_server==selected);
      QA(control_command("SERVERSTATUS 2",reply,sizeof(reply))==0 && strstr(reply,"unread=5") && !strstr(reply,"second-token"));
      QA(control_command("ROOMS 2",reply,sizeof(reply))==0 && strstr(reply,"next=-1") && strstr(reply,"room\t"));
      QA(control_command("ROOMS 2 -1",reply,sizeof(reply))==10 && control_command("SERVERSTATUS 9",reply,sizeof(reply))==10);
      QA(control_command("DOCKSTATUS",reply,16)==20);
      QA(control_command("CAPABILITIES",reply,sizeof(reply))==0 && strstr(reply,"protocol=2") && strstr(reply,"unread_scope=all_subscriptions"));
    }
    qa_trace("QA qualified connection and room-unread notifications\n");
    { struct MsgPort *listener=CreateMsgPort(); struct RexxMsg *event; RcRoomList delta;
      QA(listener && arexx_start(control_command));
      if(listener) {
        listener->mp_Node.ln_Name=(STRPTR)"AR.QA.SERVERS"; AddPort(listener);
        QA(arexx_subscribe("AR.QA.SERVERS"));
        server_event(1,0,"Connected second server");
        event=(struct RexxMsg *)GetMsg(listener);
        QA(event && strstr(event->rm_Args[0],"SERVERSTATUS \"2\" \"0\""));
        if(event) { event->rm_Result1=event->rm_Result2=0; ReplyMsg((struct Message *)event); arexx_handle(); }
        rc_roomlist_init(&delta); QA(rc_roomlist_copy(&delta,&G.servers[1].rooms)==0);
        if(delta.count) {
          delta.items[0].unread=9; dock_room_delta(1,&delta);
          event=(struct RexxMsg *)GetMsg(listener);
          QA(event && strstr(event->rm_Args[0],"ROOMUNREAD \"2:") && strstr(event->rm_Args[0],"unread=9"));
          if(event) { event->rm_Result1=event->rm_Result2=0; ReplyMsg((struct Message *)event); arexx_handle(); }
        }
        rc_roomlist_free(&delta); arexx_unsubscribe("AR.QA.SERVERS"); RemPort(listener); DeleteMsgPort(listener);
      }
      arexx_stop();
    }
    { int slot; char rid[64]; int current_unread=G.rooms.items[0].unread;
      G.server_view=0; G.rooms.items[0].unread=0;
      G.servers[1].expanded=0; strcpy(G.filter,"does-not-match");
      QA(unread_target(&slot,rid,sizeof(rid)) && slot==1 && !strcmp(rid,"r1"));
      QA(G.servers[1].rooms.items[0].unread==5 && G.active_server==0);
      G.rooms.items[0].unread=current_unread; G.filter[0]=0; G.servers[1].expanded=1; G.server_view=1;
    }
    qa_trace("QA same room IDs, late replies and draft isolation\n");
    m=netmsg_new(NC_OPEN_ROOM); m->server_slot=1; m->server_generation=G.servers[1].generation;
    m->msg.mn_Node.ln_Type=NT_REPLYMSG; strcpy(m->rid,"r1"); m->a_interval=99;
    m->msgs.items=ar_calloc(1,sizeof(RcMsg)); m->msgs.count=m->msgs.cap=1; qa_message(m->msgs.items,999);
    ar_free(m->msgs.items[0].text); m->msgs.items[0].text=ar_strdup("message from second server");
    G.pending++; PutMsg(G.port,&m->msg); handle_port();
    QA(G.servers[1].msgs.count==1 && G.msgs.count>1);
    server_select(1); composer_capture(); QA(G.active_server==1 && !strncmp(G.hook_buf,"draft for second server",23));
    QA(G.msgs.count==1 && !strcmp(G.msgs.items[0].text,"message from second server") && !strcmp(G.prefs.token,"second-token"));
    server_select(0); composer_capture(); QA(!strncmp(G.hook_buf,"draft for first server",22) && !strcmp(G.prefs.token,"first-token"));
    m=netmsg_new(NC_OPEN_ROOM); m->server_slot=1; m->server_generation=0; m->a_interval=99;
    m->msg.mn_Node.ln_Type=NT_REPLYMSG; strcpy(m->rid,"r1"); G.pending++; PutMsg(G.port,&m->msg); handle_port();
    QA(G.servers[1].msgs.count==1);
    qa_trace("QA offline roots, event view and right-click routing\n");
    server_event(1,RC_ERR_NET,"Cannot resolve host name chat.two.test"); rooms_rebuild();
    { int root=-1,children=0; struct IBox box; struct IntuiMessage point={0};
      for(i=0;i<G.side.n;i++) if(G.side.r[i].server_slot==1) {
          if(G.side.r[i].kind==SR_SERVER) { root=i; QA(G.side.r[i].connection_state==3); }
          else children++;
      }
      QA(root>=0 && children==0);
      redraw_dirty(); QA(area_box(G.g[G_ROOMS],&box));
      if(root>=0) {
          int y=(root-G.side.top)*G.side.row_h+G.side.row_h/2;
          on_side_click(40,y); redraw_dirty();
          QA(G.active_server==1 && G.server_view);
          QA(strstr(buf_str(&G.chat.pool),"Cannot resolve host name")!=NULL);
          root=0; for(i=0;i<G.side.n;i++) if(G.side.r[i].kind==SR_SERVER && G.side.r[i].server_slot==1) root=i;
          point.Class=IDCMP_MOUSEMOVE; point.MouseX=box.Left+40; point.MouseY=box.Top+(root-G.side.top)*G.side.row_h+2;
          sidebar_pointer(&point); QA(G.win->Flags&WFLG_RMBTRAP);
          point.Class=IDCMP_MOUSEBUTTONS; point.Code=MENUDOWN; sidebar_pointer(&point);
          QA(G.side_menu_pending && G.side_menu_slot==1); G.side_menu_pending=0;
          point.Class=IDCMP_MOUSEMOVE; point.MouseX=box.Left+box.Width+30;
          sidebar_pointer(&point); QA(!(G.win->Flags&WFLG_RMBTRAP));
      }
    }
    server_event(1,0,"Connection restored"); rooms_rebuild();
    { int children=0; for(i=0;i<G.side.n;i++) if(G.side.r[i].server_slot==1 && G.side.r[i].kind!=SR_SERVER) children++; QA(children>0); }
    server_select(0); rooms_rebuild();
    qa_trace("QA live server list Use and Save semantics\n");
    *next=G.prefs; QA(prefs_save(next)==0);
    QA(prefs_load(loaded)==0 && !strcmp(prefs_extra(loaded,"workspace.sidebar",""),"42") && !strcmp(prefs_extra(loaded,"workspace.0.tree",""),"5"));
    G.prefs_service=prefs_service_start(&G.prefs); QA(G.prefs_service!=NULL);
    next->servers[1].autoconnect=1;
    QA(prefs_runtime_send(next,0)==1); handle_preferences();
    QA(G.session_override && G.prefs.servers[1].autoconnect);
    QA(prefs_load(loaded)==0 && !loaded->servers[1].autoconnect);
    QA(prefs_save(next)==0 && prefs_runtime_send(next,1)==1); handle_preferences();
    QA(!G.session_override && prefs_load(loaded)==0 && loaded->servers[1].autoconnect);
    { ServerPrefs edit=G.prefs.servers[1]; int interval=G.prefs.interval;
      strcpy(edit.name,"Edited second server");
      G.settings_override=G.session_override=1; G.prefs.interval=17;
      QA(prefs_runtime_send_server(1,&edit,0)==1); handle_preferences();
      QA(G.prefs.interval==17 && G.session_override && !strcmp(G.prefs.servers[1].name,"Edited second server"));
      QA(prefs_save_server(1,&edit)==0 && prefs_load(loaded)==0);
      QA(loaded->interval==interval && !strcmp(loaded->servers[0].url,"https://chat.one.test") && !strcmp(loaded->servers[1].name,"Edited second server"));
      QA(prefs_runtime_send_server(1,&edit,1)==1); handle_preferences();
      QA(G.prefs.interval==17 && G.session_override && !G.server_overrides);
      G.prefs.interval=interval; G.settings_override=G.session_override=0;
    }
    prefs_service_stop(G.prefs_service); G.prefs_service=NULL;
    { char reply[1024];
      QA(control_command("SERVERS",reply,sizeof(reply))==0 && strstr(reply,"chat.one.test") && strstr(reply,"chat.two.test"));
      QA(control_command("SERVER 2",reply,sizeof(reply))==0 && G.active_server==1);
      QA(control_command("SERVER 1",reply,sizeof(reply))==0 && G.active_server==0);
    }
    { PrefsPanel panel;
      QA(prefs_panel_build(&panel));
      if(panel.win) {
          prefs_panel_select(&panel,6); QA(panel.server_count==2);
          panel.plugin_draft->servers[1].autoconnect=1; prefs_servers_rows(&panel);
          QA(prefs_panel_read(&panel,loaded) && loaded->servers[1].autoconnect);
          if(show) { qa_trace("QA visual server preferences\n"); Delay(50); }
          prefs_panel_dispose(&panel);
      }
    }
    if(show) { QA(worker_start(G.port,err,sizeof(err))==0); G.running_client=1; G.prefs_service=prefs_service_start(&G.prefs); G.servers[1].connected=0; server_event(1,RC_ERR_NET,"Cannot resolve host name chat.two.test"); server_select(1); rooms_rebuild(); msgs_rebuild(1); update_title(); redraw_dirty(); qa_trace("QA visual server tree\n"); for(i=0;i<7500 && !G.quit;i++) { handle_main(); handle_port(); handle_preferences(); input_events(); if(G.dirty) redraw_dirty(); Delay(2); } }
    if(show) { prefs_service_stop(G.prefs_service); G.prefs_service=NULL; G.running_client=0; }
    qa_trace("QA worker routes independent server slots\n");
    QA(worker_start(G.port,err,sizeof(err))==0);
    for(i=0;i<2;i++) {
        m=netmsg_new(NC_DISCONNECT); m->server_slot=i; m->server_generation=10+i;
        worker_send(m); WaitPort(G.port); m=(NetMsg *)GetMsg(G.port);
        QA(m && m->server_slot==i && m->server_generation==10+i && m->rc==RC_OK); netmsg_free(m);
    }

    qa_trace("QA next unread opens a filtered, collapsed room on another server\n");
    { int expected=G.pending;
      G.running_client=1; G.connected=1; G.server_view=0;
      G.servers[1].connected=1; G.servers[1].last_error=0; G.servers[1].expanded=0;
      G.rooms.items[0].unread=0; G.servers[1].rooms.items[0].unread=1;
      strcpy(G.filter,"hidden"); next_unread();
      QA(G.active_server==1 && !G.server_view && !G.filter[0] && G.servers[1].expanded && !strcmp(G.cur_rid,"r1"));
      composer_capture(); QA(!strncmp(G.hook_buf,"draft for second server",23));
      for(i=0;i<100 && G.pending>expected;i++) { handle_port(); Delay(1); }
      QA(G.pending==expected); server_select(0);
      for(i=0;i<100 && G.pending>expected;i++) { handle_port(); Delay(1); }
      QA(G.pending==expected); G.running_client=0;
    }
    { char reply[1024]; int selected=G.active_server,old_generation=G.servers[1].generation,expected=G.pending;
      G.running_client=1;
      QA(control_command("CONNECT 0",reply,sizeof(reply))==10 && control_command("DISCONNECT 9",reply,sizeof(reply))==10);
      G.servers[1].connected=0; G.prefs.servers[1].token[0]=0;
      QA(control_command("CONNECT 2",reply,sizeof(reply))==10 && G.pending==expected && G.active_server==selected);
      QA(control_command("DISCONNECT 2",reply,sizeof(reply))==0 && G.active_server==selected && G.servers[1].generation==old_generation+1);
      for(i=0;i<100 && G.pending>expected;i++) { handle_port(); Delay(1); }
      QA(G.pending==expected && !G.servers[1].connected);
      G.running_client=0;
    }
    worker_stop();
    qa_trace("QA failed background sends, expired login and draft preservation\n");
    { NetMsg *failed=netmsg_new(NC_SEND); int state=G.servers[1].last_error;
      ar_free(G.servers[1].draft); G.servers[1].draft=ar_strdup("keep my text");
      failed->server_slot=1; failed->server_generation=G.servers[1].generation;
      failed->a_text=ar_strdup("keep my text"); strcpy(failed->rid,"another-room");
      failed->rc=RC_ERR_NET; strcpy(failed->text,"Delivery unconfirmed; check before resending");
      server_background(failed);
      QA(!strcmp(G.servers[1].draft,"keep my text") && !G.servers[1].send_pending && G.servers[1].last_error==state);
      QA(strstr(G.servers[1].status,"Delivery unconfirmed") && G.servers[1].events.count>0);
      failed->rc=RC_OK; server_background(failed); QA(!strcmp(G.servers[1].draft,"keep my text"));
      failed->kind=NE_STATUS; failed->rc=RC_ERR_AUTH; G.servers[1].reconnecting=1;
      server_background(failed);
      QA(!G.servers[1].connected && !G.servers[1].reconnecting && !G.prefs.servers[1].token[0] && !strcmp(G.servers[1].draft,"keep my text"));
      netmsg_free(failed);
    }
    servers_cleanup(); G.multi=0; G.server_view=0; G.active_server=0; G.prefs=*saved; G.pending=old_pending;
    prefs_save(saved); rooms_rebuild();
cleanup:
    if(saved) ar_free(saved);
    if(next) ar_free(next);
    if(loaded) ar_free(loaded);
}

/* Real ReAction and DOS checks, all recovery files confined to RAM:. */
static void qa_conversation_features(void)
{
    Prefs saved=G.prefs; char composer[INPUT_MAX+1],uid[64],health[128]; int cursor=G.hook_pos;
    const char *original_path=draft_path; ServerState *state=&G.servers[G.active_server];
    int connected=G.connected;
    int cooling=state->cooling,retry=state->health_retry; ar_ms until=state->retry_until;
    qa_trace("QA memory budgets, connection health and draft recovery\n");
    composer_capture(); ar_strlcpy(composer,G.hook_buf,sizeof(composer)); ar_strlcpy(uid,G.presence_user_id,sizeof(uid));
    G.prefs.low_memory=1; G.prefs.message_limit=1000; G.prefs.image_cache_kib=8192;
    QA(MAX_MESSAGES==200 && memory_image_limit()==512UL*1024);
    QA(avatar_get("must-not-download",32)==NULL);
    G.prefs.message_limit=50; QA(MAX_MESSAGES==50);
    G.prefs=saved;
    G.connected=1;
    state->cooling=1; state->health_retry=1; state->retry_until=amiga_local_ms()+60000;
    QA(strstr(server_health(G.active_server,health,sizeof(health)),"Cooling down")!=NULL);
    state->cooling=0; QA(strstr(server_health(G.active_server,health,sizeof(health)),"retry in")!=NULL);
    G.connected=0; state->reconnecting=0; QA(strstr(server_health(G.active_server,health,sizeof(health)),"Offline")!=NULL);
    G.connected=connected;
    state->cooling=cooling; state->health_retry=retry; state->retry_until=until;
    draft_path="RAM:AmiRocket-features.drafts"; DeleteFile(draft_path); DeleteFile("RAM:AmiRocket-features.drafts.bak");
    strcpy(G.prefs.server,"https://drafts.example.test"); strcpy(G.presence_user_id,"account-A");
    G.draft_key[0]=G.draft_last[0]=0; draft_start(); QA(G.drafts && G.tick_open);
    draft_bind("room-one"); composer_set("first\n  code indentation",9); draft_checkpoint();
    draft_bind("room-two"); composer_capture(); QA(!G.hook_buf[0] || !strcmp(G.hook_buf,"\n"));
    composer_set("second room",4); draft_checkpoint(); draft_bind("room-one"); composer_capture();
    QA(!strcmp(G.hook_buf,"first\n  code indentation") && G.hook_pos==9);
    draft_stop(); G.draft_key[0]=G.draft_last[0]=0; draft_start(); draft_bind("room-one"); composer_capture();
    QA(!strcmp(G.hook_buf,"first\n  code indentation"));
    strcpy(G.presence_user_id,"account-B"); draft_bind("room-one"); composer_capture(); QA(!G.hook_buf[0] || !strcmp(G.hook_buf,"\n"));
    composer_set("other account",5); draft_checkpoint(); strcpy(G.presence_user_id,"account-A"); draft_bind("room-one"); composer_capture();
    QA(!strcmp(G.hook_buf,"first\n  code indentation"));
    QA(Rename(draft_path,"RAM:AmiRocket-features.drafts.bak"));
    { ArDrafts recovered={0}; QA(!draft_read(&recovered) && drafts_get(&recovered,G.draft_key)); drafts_free(&recovered); }
    draft_sent(G.active_server,"room-one","first\n  code indentation");
    { ArDrafts recovered={0}; QA(!draft_read(&recovered) && !drafts_get(&recovered,G.draft_key)); drafts_free(&recovered); }
    composer_set("",0); draft_checkpoint();
    { int n; char key[40]; drafts_free(G.drafts);
      for(n=0;n<AR_DRAFT_COUNT;n++) { snprintf(key,sizeof(key),"occupied-%d",n); drafts_set(G.drafts,key,"retained",0); }
      strcpy(G.draft_key,"https://quota.test\nuser\ncurrent"); G.draft_last[0]=0; composer_set("keep this text",5);
      QA(!draft_checkpoint()); composer_capture(); QA(!strcmp(G.hook_buf,"keep this text"));
      QA(!draft_bind("another-room")); composer_capture(); QA(!strcmp(G.hook_buf,"keep this text"));
      G.draft_key[0]=0;
    }
    draft_stop(); DeleteFile(draft_path); DeleteFile("RAM:AmiRocket-features.drafts.bak"); DeleteFile("RAM:AmiRocket-features.drafts.new");
    G.draft_warned=0; G.draft_key[0]=G.draft_last[0]=0; draft_path=original_path; G.prefs=saved;
    ar_strlcpy(G.presence_user_id,uid,sizeof(G.presence_user_id)); composer_set(composer,cursor);
    qa_trace("QA native paged search results and stale reply rejection\n");
    { Object *root,*obj; NetMsg *m=netmsg_new(NC_SEARCH); struct Window *win;
      NewList(&G.search_nodes); rc_msglist_init(&G.search_results);
      root=VLayoutObject,LAYOUT_AddChild,G.search_info=panel_button(756,"Search"),CHILD_WeightedHeight,0,
        LAYOUT_AddChild,G.search_list=ListBrowserObject,LISTBROWSER_Labels,(ULONG)&G.search_nodes,End,
        LAYOUT_AddChild,G.search_preview=TextEditorObject,GA_TEXTEDITOR_ReadOnly,TRUE,End,End;
      qa_trace("QA search window opening\n");obj=panel_window(root,"Search checks",500); win=obj?RA_OpenWindow(obj):NULL;qa_trace("QA search window opened\n"); QA(win && m);
      if(win && m) {
        G.search_active=1; G.search_win=win; G.search_slot=G.active_server; G.search_generation=G.servers[G.active_server].generation;
        strcpy(G.search_room,"room-one"); G.search_serial=77;
        m->server_slot=G.search_slot; m->server_generation=G.search_generation; m->a_before=77; m->a_interval=25; m->a_background=51;
        strcpy(m->rid,"room-one"); m->a_text=ar_strdup("needle"); m->msgs.items=ar_calloc(1,sizeof(RcMsg)); m->msgs.count=m->msgs.cap=1; qa_message(m->msgs.items,1);
        qa_trace("QA search result update\n");G.search_busy=1; on_search(m);qa_trace("QA search result updated\n"); QA(!G.search_busy && G.search_results.count==1 && G.search_offset==25 && G.search_total==51);
        { STRPTR value=(STRPTR)DoGadgetMethod((struct Gadget *)G.search_preview,win,NULL,GM_TEXTEDITOR_ExportText,NULL);
          QA(value && strstr((char *)value,"readable chat message")); if(value) FreeVec(value); }
        m->a_before=76; m->a_interval=0; on_search(m); QA(G.search_results.count==1 && G.search_offset==25);
      }
      qa_trace("QA search disposal\n");search_dispose(obj);if(m)netmsg_free(m);qa_trace("QA search disposed\n");
    }
}

static void qa_message_actions(void)
{
    Object *obj,*emoji; struct Window *win; RcMsg own={0},other={0}; NetMsg result={0};
    int saved_slot=G.active_server; ServerPrefs saved=G.prefs.servers[saved_slot]; char me[96];
    ULONG disabled,height=0;
    qa_trace("QA composer divider, notifications and message/thread actions\n");
    ar_strlcpy(me,G.username,sizeof(me)); strcpy(G.username,"tommy");
    G.prefs.servers[saved_slot].quiet=0; G.prefs.servers[saved_slot].notify_mentions=1;
    QA(notification_allowed(saved_slot,"room","alice","hello @tommy!"));
    QA(!notification_allowed(saved_slot,"room","alice","hello @tommy2"));
    QA(!notification_allowed(saved_slot,"room","tommy","@all"));
    QA(notification_allowed(saved_slot,"room","alice","@here hello"));
    G.prefs.servers[saved_slot].quiet=1; QA(!notification_allowed(saved_slot,"room","alice","@all"));
    G.prefs.servers[saved_slot]=saved; ar_strlcpy(G.username,me,sizeof(G.username));
    qa_trace("QA action composer geometry\n");
    QA(composer_rows()>=2 && composer_rows()<=12);
    GetAttr(GA_Height,G.g[G_INPUT],&height);
    QA(height>=(ULONG)(G.scr->RastPort.TxHeight+2)*2);
    { char saved_rows[16]; ar_strlcpy(saved_rows,prefs_extra(&G.prefs,"workspace.composer_rows","4"),sizeof(saved_rows));
      remember_window(); QA(composer_rows()>=2 && composer_rows()<=12); prefs_set(&G.prefs,"workspace.composer_rows",saved_rows); }
    qa_trace("QA action window construction\n");
    NewList(&G.action_nodes); rc_msglist_init(&G.action_results);
    obj=message_action_window(&emoji);qa_trace("QA action window opening\n"); win=obj?RA_OpenWindow(obj):NULL;qa_trace("QA action window opened\n"); QA(win!=NULL);
    if(!win) { if(obj) DisposeObject(obj); return; }
    G.action_win=win; G.action_active=1; G.action_slot=saved_slot; G.action_generation=G.servers[saved_slot].generation;
    strcpy(G.action_room,"QA-ROOM"); strcpy(G.action_thread,"parent"); strcpy(G.action_me,"tommy"); G.action_serial++;
    own.id="parent"; own.username="tommy"; own.text="original\nsecond line";
    action_target(&own); GetAttr(GA_Disabled,G.action_edit,&disabled); QA(!disabled && action_owned());
    other.id="other"; other.username="alice"; other.text="Someone else's message";
    action_target(&other); GetAttr(GA_Disabled,G.action_edit,&disabled); QA(disabled && !action_owned());
    SetGadgetAttrs((struct Gadget *)G.action_editor,win,NULL,GA_TEXTEDITOR_Contents,(ULONG)"keep my draft",TAG_END);
    result.kind=NC_THREAD; result.rc=RC_OK; result.server_slot=saved_slot; result.server_generation=G.action_generation;
    result.a_before=G.action_serial; strcpy(result.rid,G.action_room); result.a_background=30;
    result.msgs.items=ar_calloc(1,sizeof(RcMsg)); result.msgs.count=result.msgs.cap=1;
    result.msgs.items[0].id=ar_strdup("reply"); result.msgs.items[0].username=ar_strdup("alice"); result.msgs.items[0].text=ar_strdup("Thread reply");
    G.action_busy=1; result.server_generation++;
    on_message_action(&result); QA(G.action_busy && !G.action_results.count);
    result.server_generation--; on_message_action(&result);
    QA(!G.action_busy && G.action_results.count==1 && G.action_total==30 && !result.msgs.count);
    { STRPTR text=(STRPTR)DoGadgetMethod((struct Gadget *)G.action_editor,win,NULL,GM_TEXTEDITOR_ExportText,NULL);
      QA(text && strstr((const char *)text,"keep my draft")); if(text) FreeVec(text); }
    action_target(&G.action_results.items[0]); QA(!strcmp(G.action_id,"reply") && !action_owned());
    qa_trace("QA action disposal\n");action_dispose(obj);qa_trace("QA action disposed\n");
}

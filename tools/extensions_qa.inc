/* Runs only inside the disposable emulator profile, never against a real server. */
static void qa_extensions(void)
{
    Prefs *saved=ar_malloc(sizeof(Prefs)),*next=ar_malloc(sizeof(Prefs)),*loaded=ar_malloc(sizeof(Prefs));
    int i,index; char result[2048];
    qa_trace("QA repeated standalone Preferences launch and return\n");
    {
        BPTR script=Open("T:AmiRocketPrefsQA",MODE_NEWFILE),marker=0;
        const char *commands="Stack 131072\nAmiRocket:AmiRocketPrefs SELFTEST\nIf WARN\n Quit 10\nEndIf\nAmiRocket:AmiRocketPrefs SELFTEST\nIf WARN\n Quit 10\nEndIf\nEcho >T:AmiRocketPrefsDone done\n";
        DeleteFile("T:AmiRocketPrefsDone");
        QA(script!=0);
        if(script) {
            Write(script,(APTR)commands,strlen(commands)); Close(script);
            QA(launch_helper("Execute T:AmiRocketPrefsQA")>=0);
            for(i=0;i<400 && !marker;i++) { Delay(5); marker=Open("T:AmiRocketPrefsDone",MODE_OLDFILE); }
            QA(marker!=0); if(marker) Close(marker);
            QA(Write(Output(),(APTR)"QA helper returned safely\n",26)==26);
            DeleteFile("T:AmiRocketPrefsQA"); DeleteFile("T:AmiRocketPrefsDone");
        }
    }
    qa_trace("QA session settings and shared configuration\n");
    QA(saved && next && loaded); if(!saved||!next||!loaded) goto cleanup;
    *saved=G.prefs; *next=G.prefs;
    QA(prefs_set(next,"workspace.data_dir","RAM:SharedAmiRocket")==0);
    QA(prefs_resource_path(next,"PROGDIR:Plugins/timestamp.arp",result,sizeof(result))==0 && !strcmp(result,"RAM:SharedAmiRocket/Plugins/timestamp.arp"));
    QA(prefs_resource_path(next,"PROGDIR:Sounds/Notification.8svx",result,sizeof(result))==0 && !strcmp(result,"RAM:SharedAmiRocket/Sounds/Notification.8svx"));
    QA(prefs_resource_path(next,"T:custom.8svx",result,sizeof(result))==0 && !strcmp(result,"T:custom.8svx"));
    *next=G.prefs;
    QA(prefs_set(next,"plugin.future.option","preserved")==0);
    QA(prefs_set(next,"request_spacing_ms","1500")==0 && prefs_set(next,"background_interval","60")==0);
    QA(prefs_set(next,"request_spacing_ms","0")<0 && prefs_set(next,"background_interval","1")<0);
    QA(prefs_set(next,"avatar_disk_cache","0")==0);
    QA(!prefs_set(next,"realtime","1") && !prefs_set(next,"low_memory","0") && !prefs_set(next,"message_limit","123") && !prefs_set(next,"image_cache_kib","768"));
    next->show_emoji=1; QA(prefs_save(next)==0);
    QA(prefs_load(loaded)==0 && loaded->request_spacing_ms==1500 && loaded->background_interval==60 && !loaded->avatar_disk_cache);

    G.prefs_service=prefs_service_start(next); QA(G.prefs_service!=NULL);
    next->show_emoji=0; next->open_links=0;
    QA(!prefs_set(next,"realtime","0") && !prefs_set(next,"low_memory","1") && !prefs_set(next,"message_limit","150") && !prefs_set(next,"image_cache_kib","512"));
    QA(prefs_runtime_send(next,0)==1); handle_preferences();
    QA(G.prefs.show_emoji==0 && !G.prefs.realtime && G.session_override && G.prefs.request_spacing_ms==1500 && G.prefs.background_interval==60 && !G.prefs.avatar_disk_cache);
    QA(prefs_load(loaded)==0 && loaded->show_emoji==1 && loaded->realtime && !loaded->low_memory && loaded->message_limit==123 && loaded->image_cache_kib==768);
    QA(prefs_runtime_get(loaded) && !loaded->realtime && loaded->show_emoji==0);
    QA(prefs_save_client_state()==0);
    QA(prefs_load(loaded)==0 && loaded->show_emoji==1 && loaded->realtime && !loaded->low_memory && loaded->message_limit==123 && loaded->image_cache_kib==768);
    QA(!strcmp(prefs_extra(loaded,"plugin.future.option",""),"preserved"));
    QA(prefs_save(next)==0 && prefs_runtime_send(next,1)==1); handle_preferences();
    QA(!G.session_override && prefs_load(loaded)==0 && loaded->show_emoji==0 && !loaded->realtime && loaded->low_memory && loaded->message_limit==150 && loaded->image_cache_kib==512);
    QA(!strcmp(prefs_extra(loaded,"plugin.future.option",""),"preserved"));
    QA(G.prefs.low_memory && G.prefs.message_limit==150 && G.prefs.image_cache_kib==512);
    QA(prefs_set(next,"message_limit","49")<0 && prefs_set(next,"image_cache_kib","9000")<0);
    QA(prefs_set(next,"interval","1")<0 && prefs_set(next,"token","forbidden")<0);
    G.transfer_dialog=1;
    QA(!control_command("STATUS",result,sizeof(result)) && strstr(result,"transfer=1"));
    QA(!control_command("GETCONFIG message_limit",result,sizeof(result)));
    QA(control_command("SETCONFIG message_limit 100",result,sizeof(result))==10 && strstr(result,"BUSY"));
    G.transfer_dialog=0;
    QA(control_command("GETCONFIG token",result,sizeof(result))==10);
    QA(control_command("SETCONFIG show_avatars 2",result,sizeof(result))==10);
    QA(control_command("SETCONFIG plugin.future.option session",result,sizeof(result))==0);
    QA(prefs_load(loaded)==0 && !strcmp(prefs_extra(loaded,"plugin.future.option",""),"preserved"));
    qa_trace("QA loadable plugin ABI and example actions\n");
    plugins_start_host();
    prefs_set(&G.prefs,"plugin.timestamp.path","AmiRocket:Plugins/timestamp.arp"); prefs_set(&G.prefs,"plugin.timestamp.enabled","1");
    prefs_set(&G.prefs,"plugin.quickreply.path","AmiRocket:Plugins/quickreply.arp"); prefs_set(&G.prefs,"plugin.quickreply.enabled","1");
    prefs_set(&G.prefs,"plugin.quickreply.text","QA quick reply");
    plugins_sync(&G.prefs); publish_plugins(); QA(plugins_count()==2);
    QA(prefs_runtime_plugins(result,sizeof(result)) && strstr(result,"|timestamp|") && strstr(result,"|quickreply|"));
    composer_set("",0);
    for(index=0;index<plugins_count() && strcmp(plugins_id(index),"timestamp");index++);
    QA(index<plugins_count()); plugins_action(index); composer_capture(); QA(G.hook_buf[0]=='[');
    composer_set("",0);
    for(index=0;index<plugins_count() && strcmp(plugins_id(index),"quickreply");index++);
    QA(index<plugins_count()); plugins_action(index); composer_capture(); QA(!strncmp(G.hook_buf,"QA quick reply",14));
    prefs_set(&G.prefs,"plugin.timestamp.enabled","0"); G.plugins_dirty=1; refresh_plugins(); QA(plugins_count()==1);
    prefs_set(&G.prefs,"plugin.quickreply.path","AmiRocket:AmiRocket-GUIQA"); plugins_sync(&G.prefs);
    QA(plugins_count()==0 && plugins_error()[0]);
    plugins_stop();
    qa_trace("QA real ARexx messages and notifications\n");
    QA(arexx_start(control_command));
    { struct MsgPort *reply=CreateMsgPort(),*listener=CreateMsgPort(),*target;
      struct RexxMsg *message,*got; const char *command="GETCONFIG show_emoji";
      QA(reply && listener);
      if(reply && listener) {
        message=CreateRexxMsg(reply,"rexx","AR.QA"); QA(message!=NULL);
        if(message) {
          message->rm_Action=RXCOMM|RXFF_RESULT; message->rm_Args[0]=CreateArgstring(command,strlen(command));
          Forbid(); target=FindPort(arexx_port_name()); if(target) PutMsg(target,(struct Message *)message); Permit();
          QA(target!=NULL); arexx_handle(); got=(struct RexxMsg *)GetMsg(reply); QA(got==message);
          if(got) {
            QA(got->rm_Result1==0 && got->rm_Result2 && !strcmp((char *)got->rm_Result2,"0"));
            if(!got->rm_Result1 && got->rm_Result2) DeleteArgstring((char *)got->rm_Result2);
            DeleteArgstring(got->rm_Args[0]); DeleteRexxMsg(got);
          }
        }
        listener->mp_Node.ln_Name=(STRPTR)"AR.QA.EVENTS"; AddPort(listener);
        QA(arexx_subscribe("AR.QA.EVENTS"));
        application_event("MESSAGE","qa","alice","new \"text\"\nline");
        got=(struct RexxMsg *)GetMsg(listener); QA(got && arexx_is_packet(got));
        if(got) {
          QA(strstr(got->rm_Args[0],"AMIROCKET_EVENT MESSAGE") && strstr(got->rm_Args[0],"new \"\"text\"\" line"));
          got->rm_Result1=got->rm_Result2=0; ReplyMsg((struct Message *)got); arexx_handle();
        }
        QA(arexx_unsubscribe("AR.QA.EVENTS"));
        application_event("MESSAGE","qa","alice","not delivered"); QA(GetMsg(listener)==NULL);

        qa_trace("QA sequenced ARexx events, slow receivers and snapshot recovery\n");
        { struct MsgPort *fast=CreateMsgPort(); unsigned long before=arexx_dropped(); int k,count=0; ArDockEvent event; char long_text[1400];
          QA(fast!=NULL);
          if(fast) {
            fast->mp_Node.ln_Name=(STRPTR)"AR.QA.FAST"; AddPort(fast);
            QA(arexx_subscribe_version("AR.QA.EVENTS",2)); QA(arexx_subscribe_version("AR.QA.FAST",2));
            arexx_handle();
            while((got=(struct RexxMsg *)GetMsg(listener))) { QA(ar_rexx_event2(got->rm_Args[0],&event) && !strcmp(event.kind,"RESYNC")); got->rm_Result1=got->rm_Result2=0; ReplyMsg((struct Message *)got); }
            while((got=(struct RexxMsg *)GetMsg(fast))) { got->rm_Result1=got->rm_Result2=0; ReplyMsg((struct Message *)got); }
            arexx_handle();
            for(k=0;k<6;k++) {
              arexx_event("MESSAGE","qa","alice","dock event");
              while((got=(struct RexxMsg *)GetMsg(fast))) { QA(ar_rexx_event2(got->rm_Args[0],&event)); count++; got->rm_Result1=got->rm_Result2=0; ReplyMsg((struct Message *)got); }
              arexx_handle();
            }
            QA(count==6 && arexx_dropped()>before && arexx_pending()==2);
            QA(control_command("DOCKSTATUS",result,sizeof(result))==0 && strstr(result,"version=2") && strstr(result,"dropped="));
            memset(long_text,'"',sizeof(long_text)-1); long_text[sizeof(long_text)-1]=0;
            arexx_event("MESSAGE",long_text,long_text,long_text);
            got=(struct RexxMsg *)GetMsg(fast);
            QA(got && ar_rexx_event2(got->rm_Args[0],&event) && event.truncated);
            if(got) { got->rm_Result1=got->rm_Result2=0; ReplyMsg((struct Message *)got); }
            count=0; while((got=(struct RexxMsg *)GetMsg(listener))) { count++; got->rm_Result1=got->rm_Result2=0; ReplyMsg((struct Message *)got); }
            QA(count==2); arexx_handle();
            got=(struct RexxMsg *)GetMsg(listener);
            QA(got && ar_rexx_event2(got->rm_Args[0],&event) && !strcmp(event.kind,"RESYNC"));
            if(got) { got->rm_Result1=got->rm_Result2=0; ReplyMsg((struct Message *)got); } arexx_handle();
            QA(GetMsg(fast)==NULL && arexx_pending()==0);
            QA(arexx_unsubscribe("AR.QA.EVENTS") && arexx_unsubscribe("AR.QA.FAST"));
            RemPort(fast); DeleteMsgPort(fast);
          }
        }
        RemPort(listener);
      }
      if(reply) DeleteMsgPort(reply);
      if(listener) DeleteMsgPort(listener);
    }
    { char epoch[80]; ar_strlcpy(epoch,arexx_epoch(),sizeof(epoch)); arexx_stop(); QA(arexx_start(control_command)); QA(strcmp(epoch,arexx_epoch())!=0); arexx_stop(); }
    prefs_service_stop(G.prefs_service); G.prefs_service=NULL;
    G.prefs=*saved; G.session_override=0; G.plugins_dirty=0; prefs_save(saved);
    /* Settings controls belong to the separate app, not the client menu. */
    for(i=0;i<BASE_MENU_ITEMS;i++) QA(!g_newmenu[i].nm_Label || g_newmenu[i].nm_Label==NM_BARLABEL || strcmp(g_newmenu[i].nm_Label,"Settings"));
cleanup:
    if(saved) { memset(saved,0,sizeof(*saved)); ar_free(saved); }
    if(next) { memset(next,0,sizeof(*next)); ar_free(next); }
    if(loaded) { memset(loaded,0,sizeof(*loaded)); ar_free(loaded); }
}

static void qa_release_polish(void)
{
    Prefs *saved=ar_malloc(sizeof(Prefs));char text[512],hash[17],other[17];int action=G.editor_action;
    qa_trace("QA low-resource preset, transfer ETA and navigation shortcuts\n");
    QA(saved!=NULL);if(!saved)return;*saved=G.prefs;
    prefs_low_resource(saved);
    QA(saved->low_memory && saved->preview_width==160 && saved->message_limit==100 && saved->image_cache_kib==256);
    QA(!saved->show_avatars && !saved->show_emoji && !saved->auto_thumbnails && saved->background_interval==300 && saved->interval==60);
    QA(!strcmp(saved->server,G.prefs.server) && !strcmp(saved->token,G.prefs.token));
    transfer_caption(2048,4096,1000,3000,text,sizeof(text));QA(strstr(text,"1.0 KiB/s") && strstr(text,"0m 2s left"));
    transfer_caption(2048,0,1000,3000,text,sizeof(text));QA(strstr(text,"size unknown")!=NULL);
    transfer_caption(1,100,1000,1000,text,sizeof(text));QA(strstr(text,"measuring")!=NULL);
    QA(editor_key(0x42,IEQUALIFIER_CONTROL) && G.editor_action==EDITOR_NEXTROOM);
    QA(editor_key(0x42,IEQUALIFIER_CONTROL|IEQUALIFIER_LSHIFT) && G.editor_action==EDITOR_PREVROOM);
    QA(editor_key(0x27,IEQUALIFIER_CONTROL) && G.editor_action==EDITOR_QUICKROOM);
    QA(editor_key(0x16,IEQUALIFIER_CONTROL) && G.editor_action==EDITOR_NEXTUNREAD);G.editor_action=action;
    {int target=navigation_target(1);QA(target<0 || (target<G.side.n && G.side.r[target].rid));}
    QA(G.g[G_RETRYSEND]!=NULL);
    send_text_hash("kept text",hash);send_text_hash("edited text",other);QA(strcmp(hash,other)!=0);
    qa_trace("QA restart-safe delivery record and account scoping\n");
    {ArDrafts *old=G.drafts,*disk=ar_calloc(1,sizeof(*disk));const char *oldpath=draft_path;char key[512],different[512],uid[64],rid[64];NetMsg sent={0};
      ar_strlcpy(uid,G.presence_user_id,sizeof(uid));ar_strlcpy(rid,G.cur_rid,sizeof(rid));*saved=G.prefs;
      draft_path="RAM:AmiRocket-delivery-qa.drafts";DeleteFile(draft_path);G.drafts=ar_calloc(1,sizeof(*G.drafts));
      strcpy(G.presence_user_id,"delivery-account");strcpy(G.cur_rid,"delivery-room");strcpy(G.prefs.server,"https://delivery.test");strcpy(G.prefs.servers[G.active_server].url,"https://delivery.test");
      QA(disk && G.drafts && send_record_key(G.active_server,G.cur_rid,key));
      if(disk && G.drafts){
        QA(draft_write_key(key,"AR-stable|0123456789abcdef",0)==0);
        QA(draft_read(disk)==0 && drafts_get(disk,key)!=NULL);
        drafts_set(G.drafts,key,"AR-stable|0123456789abcdef",0);
        strcpy(G.presence_user_id,"other-account");QA(send_record_key(G.active_server,G.cur_rid,different) && strcmp(key,different));
        strcpy(G.presence_user_id,"delivery-account");strcpy(sent.rid,G.cur_rid);sent.a_uid="wrong-id";send_record_done(G.active_server,&sent);QA(drafts_get(G.drafts,key)!=NULL);
        sent.a_uid="AR-stable";send_record_done(G.active_server,&sent);QA(drafts_get(G.drafts,key)==NULL);
        drafts_free(disk);QA(draft_read(disk)==0 && !drafts_get(disk,key));
      }
      if(disk){drafts_free(disk);ar_free(disk);}if(G.drafts){drafts_free(G.drafts);ar_free(G.drafts);}G.drafts=old;
      DeleteFile(draft_path);DeleteFile("RAM:AmiRocket-delivery-qa.drafts.bak");draft_path=oldpath;G.prefs=*saved;
      ar_strlcpy(G.presence_user_id,uid,sizeof(uid));ar_strlcpy(G.cur_rid,rid,sizeof(rid));send_state_refresh();
    }
    ar_free(saved);
}

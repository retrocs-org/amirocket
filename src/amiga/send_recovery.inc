/* Delivery IDs live beside private draft text, never in diagnostic reports. */
static int send_record_key(int slot,const char *rid,char key[512])
{
    const ServerPrefs *p=&G.prefs.servers[slot];
    const char *uid=slot==G.active_server?G.presence_user_id:G.servers[slot].presence_user_id;
    if(!*uid)uid=G.multi?p->user_id:G.prefs.user_id;
    if(!*uid || !rid || !*rid)return 0;
    return snprintf(key,512,"send\n%s\n%s\n%s",G.multi?p->url:G.prefs.server,uid,rid)<512;
}
static void send_text_hash(const char *text,char out[17])
{
    ULONG a=2166136261UL,b=5381;const unsigned char *p=(const unsigned char *)text;
    while(*p){a=(a^*p)*16777619UL;b=(b*33)^*p++;}
    snprintf(out,17,"%08lx%08lx",(unsigned long)a,(unsigned long)b);
}
static void send_state_refresh(void)
{
    char key[512];int recover=G.drafts && send_record_key(G.active_server,G.cur_rid,key) && drafts_get(G.drafts,key)!=NULL;
    if(!G.win)return;
    if(G.g[G_RETRYSEND])SetGadgetAttrs((struct Gadget *)G.g[G_RETRYSEND],G.win,NULL,GA_Text,(ULONG)(G.send_pending?"Sending...":recover?"Retry unconfirmed send...":"Retry send..."),GA_Disabled,!recover || G.send_pending || !G.connected,TAG_END);
    if(G.g[G_SEND])SetGadgetAttrs((struct Gadget *)G.g[G_SEND],G.win,NULL,GA_Disabled,G.send_pending || G.server_view,TAG_END);
}
static int send_record_prepare(NetMsg *m)
{
    char key[512],hash[17],value[128],id[96];const ArDraft *saved;const char *sep;static ULONG serial;
    struct DateStamp now;
    if(!G.drafts || !send_record_key(G.active_server,G.cur_rid,key))return 0;
    saved=drafts_get(G.drafts,key);send_text_hash(m->a_text,hash);
    if(saved && (sep=strchr(saved->text,'|'))!=NULL && (size_t)(sep-saved->text)<sizeof(id) && !strcmp(sep+1,hash)){
        memcpy(id,saved->text,sep-saved->text);id[sep-saved->text]=0;m->a_verify=1;
        {struct EasyStruct es={sizeof(es),0,"Retry unconfirmed message","Check delivery and retry this message? Its original delivery ID will be reused.","Check and retry|Cancel"};
         if(EasyRequest(G.win,&es,NULL)==0)return 0;}
    }else{
        if(saved){struct EasyStruct es={sizeof(es),0,"Previous delivery unconfirmed","The earlier text may already have arrived. Send your edited text as a NEW message?","Send new message|Cancel"};if(EasyRequest(G.win,&es,NULL)==0)return 0;}
        DateStamp(&now);snprintf(id,sizeof(id),"AR%08lx%08lx%08lx%08lx%08lx",(ULONG)now.ds_Days,(ULONG)now.ds_Minute,(ULONG)now.ds_Tick,(ULONG)FindTask(NULL),++serial);
    }
    m->a_uid=ar_strdup(id);if(!m->a_uid)return 0;
    if(!draft_checkpoint() || G.draft_warned)return 0;
    snprintf(value,sizeof(value),"%s|%s",id,hash);
    if(draft_write_key(key,value,0)<0)return 0;
    if(drafts_set(G.drafts,key,value,0)<0)return 0;
    return 1;
}
static void send_record_done(int slot,const NetMsg *m)
{
    char key[512];const ArDraft *saved;
    if(!m->a_uid || !G.drafts || !send_record_key(slot,m->rid,key))return;
    saved=drafts_get(G.drafts,key);
    if(saved && !strncmp(saved->text,m->a_uid,strlen(m->a_uid)) && saved->text[strlen(m->a_uid)]=='|'){
        if(!draft_write_key(key,"",0))drafts_set(G.drafts,key,"",0);
    }
}
static void retry_failed_send(void)
{
    char key[512];
    if(G.send_pending)return;
    if(!G.drafts || !send_record_key(G.active_server,G.cur_rid,key) || !drafts_get(G.drafts,key)){set_status("No unconfirmed send in this conversation");return;}
    cmd_send_input();
}

static void forget_failed_send(void)
{
    char key[512];struct EasyStruct es={sizeof(es),0,"Forget delivery record","Check the conversation first. Forgetting this record keeps your draft, but a later send gets a NEW ID and may duplicate an already delivered message.","Forget record|Cancel"};
    if(G.send_pending || !G.drafts || !send_record_key(G.active_server,G.cur_rid,key) || !drafts_get(G.drafts,key))return;
    if(!EasyRequest(G.win,&es,NULL))return;
    if(!draft_write_key(key,"",0)){drafts_set(G.drafts,key,"",0);send_state_refresh();}
    else set_status("Could not forget delivery record; it has been kept");
}

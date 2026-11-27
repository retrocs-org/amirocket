/* Draft data is separate from configuration. Shared-file updates merge one key
 * under the same cross-process lock used for preferences, then replace atomically. */
static char installed_draft_path[512];
static const char *draft_path="PROGDIR:AmiRocket.drafts";
static int draft_read_path(ArDrafts *out,const char *path)
{
    struct Process *process=(struct Process *)FindTask(NULL); APTR window=process->pr_WindowPtr;
    BPTR file; Buf data; char chunk[2048]; LONG n; int rc=0;
    process->pr_WindowPtr=(APTR)-1; file=Open(path,MODE_OLDFILE);
    if(!file) { LONG error=IoErr(); process->pr_WindowPtr=window; return error==ERROR_OBJECT_NOT_FOUND?1:-1; }
    buf_init(&data);
    while((n=Read(file,chunk,sizeof(chunk)))>0) {
        if(data.len+(size_t)n>AR_DRAFT_BYTES || buf_append(&data,chunk,n)<0) { rc=-1; break; }
    }
    if(n<0) rc=-1;
    Close(file);
    if(!rc) rc=drafts_decode(out,buf_str(&data),data.len);
    buf_free(&data); process->pr_WindowPtr=window; return rc;
}
static int draft_read(ArDrafts *out)
{
    int primary=draft_read_path(out,draft_path),backup; char path[600];
    if(!primary) return 0;
    snprintf(path,sizeof(path),"%s.bak",draft_path); backup=draft_read_path(out,path);
    return !backup?0:primary<0 || backup<0?-1:1;
}
static int draft_write_key(const char *key,const char *text,int cursor)
{
    ArDrafts *disk=ar_calloc(1,sizeof(*disk)); Buf encoded; int rc=-1,old=0; BPTR file=0,lock;
    char temp[600],backup[600]; struct Process *process=(struct Process *)FindTask(NULL); APTR window=process->pr_WindowPtr;
    buf_init(&encoded); if(!disk || prefs_lock()<0) goto done;
    process->pr_WindowPtr=(APTR)-1;
    if(draft_read(disk)<0 || drafts_set(disk,key,text,cursor)<0 || drafts_encode(disk,&encoded)<0) goto unlock;
    snprintf(temp,sizeof(temp),"%s.new",draft_path); snprintf(backup,sizeof(backup),"%s.bak",draft_path);
    file=Open(temp,MODE_NEWFILE); if(!file) goto unlock;
    { LONG written=Write(file,encoded.data,encoded.len); LONG closed=Close(file); file=0;
      if(written!=(LONG)encoded.len || !closed) { DeleteFile(temp); goto unlock; } }
    lock=Lock(draft_path,ACCESS_READ); if(lock) { UnLock(lock); old=1; DeleteFile(backup); if(!Rename(draft_path,backup)) { DeleteFile(temp); goto unlock; } }
    if(!Rename(temp,draft_path)) { if(old) Rename(backup,draft_path); DeleteFile(temp); goto unlock; }
    DeleteFile(backup); rc=0;
unlock: process->pr_WindowPtr=window; prefs_unlock();
done: if(file) Close(file); if(disk) { drafts_free(disk); ar_free(disk); } buf_free(&encoded); return rc;
}
static int draft_checkpoint(void)
{
    Buf text; ArDraft *entry; int rc;
    if(!G.drafts || !G.draft_key[0] || !G.g[G_INPUT]) return 1;
    composer_capture(); if(G.composer_overlong) { if(!G.draft_warned) server_notice(G.active_server,"Draft recovery limit is 4000 characters; copy or shorten this text"); G.draft_warned=1; mark_dirty(DIRTY_HEADER); return 0; }
    buf_init(&text);
    if(strspn(G.hook_buf," \t\r\n")!=strlen(G.hook_buf)) latin1_to_utf8(&text,G.hook_buf);
    entry=(ArDraft *)drafts_get(G.drafts,G.draft_key);
    if(!strcmp(buf_str(&text),G.draft_last) && G.draft_cursor==G.hook_pos && (!entry || !entry->dirty)) { buf_free(&text); return 1; }
    if(text.oom || drafts_pending(G.drafts,G.draft_key,buf_str(&text),G.hook_pos)<0) { buf_free(&text); if(!G.draft_warned) server_notice(G.active_server,"Draft capacity reached; copy or clear text before switching rooms"); G.draft_warned=1; mark_dirty(DIRTY_HEADER); return 0; }
    entry=(ArDraft *)drafts_get(G.drafts,G.draft_key); if(entry) entry->dirty=1;
    rc=draft_write_key(G.draft_key,buf_str(&text),G.hook_pos);
    if(!rc) {
        drafts_commit(G.drafts,G.draft_key);
        ar_strlcpy(G.draft_last,buf_str(&text),sizeof(G.draft_last)); G.draft_cursor=G.hook_pos; if(G.draft_warned) mark_dirty(DIRTY_HEADER); G.draft_warned=0;
    } else if(!G.draft_warned) { server_notice(G.active_server,"Draft kept in memory; could not save recovery data (disk, capacity or invalid file)"); G.draft_warned=1; mark_dirty(DIRTY_HEADER); }
    buf_free(&text); return 1;
}
static int draft_bind(const char *rid)
{
    char key[512]; const ArDraft *saved; Buf latin; const char *uid=G.presence_user_id[0]?G.presence_user_id:G.prefs.user_id;
    ArDrafts *fresh; int i,loaded;
    if(!G.drafts || !rid || !*rid || !uid[0]) return 1;
    snprintf(key,sizeof(key),"%s\n%s\n%s",G.prefs.server,uid,rid);
    if(!strcmp(key,G.draft_key)) return 1;
    if(!draft_checkpoint()) return 0;
    fresh=ar_calloc(1,sizeof(*fresh)); if(!fresh) return 0;
    loaded=-1;
    if(prefs_lock()==0) { loaded=draft_read(fresh); prefs_unlock(); }
    if(loaded>=0) {
        for(i=0;i<AR_DRAFT_COUNT;i++) if(G.drafts->entries[i].key && G.drafts->entries[i].dirty) {
            ArDraft *pending=&G.drafts->entries[i],*merged;
            if(drafts_pending(fresh,pending->key,pending->text,pending->cursor)<0) { drafts_free(fresh); ar_free(fresh); return 0; }
            merged=(ArDraft *)drafts_get(fresh,pending->key); if(merged) merged->dirty=1;
        }
        drafts_free(G.drafts); *G.drafts=*fresh; memset(fresh,0,sizeof(*fresh));
    }
    drafts_free(fresh); ar_free(fresh);
    ar_strlcpy(G.draft_key,key,sizeof(G.draft_key));
    saved=drafts_get(G.drafts,key); buf_init(&latin);
    if(saved) utf8_to_latin1(&latin,saved->text,1);
    composer_set(buf_str(&latin),saved?saved->cursor:0);
    ar_strlcpy(G.draft_last,saved?saved->text:"",sizeof(G.draft_last)); G.draft_cursor=saved?saved->cursor:0;
    G.draft_warned=saved && saved->dirty; mark_dirty(DIRTY_HEADER); buf_free(&latin); return 1;
}
static void draft_sent(int slot,const char *rid,const char *text)
{
    char key[512]; const ArDraft *saved; Buf copy; ServerPrefs *p; const char *uid;
    if(!G.drafts || slot<0 || slot>=PREFS_SERVERS_MAX || !rid || !text) return;
    p=&G.prefs.servers[slot]; uid=slot==G.active_server?G.presence_user_id:G.servers[slot].presence_user_id;
    if(!*uid) uid=p->user_id;
    snprintf(key,sizeof(key),"%s\n%s\n%s",G.multi?p->url:G.prefs.server,uid,rid); saved=drafts_get(G.drafts,key); if(!saved) return;
    buf_init(&copy); buf_puts(&copy,saved->text);
    if(!strcmp(ar_trim(copy.data),text)) { drafts_pending(G.drafts,key,"",0); if(!draft_write_key(key,"",0)) drafts_commit(G.drafts,key); }
    buf_free(&copy);
}
static ULONG client_tick_signal(void) { return G.tick_port?1UL<<G.tick_port->mp_SigBit:0; }
static void client_tick_handle(void)
{
    if(!G.tick || !G.tick_pending || !CheckIO((struct IORequest *)G.tick)) return;
    WaitIO((struct IORequest *)G.tick); G.tick_pending=0;
    if(G.status_until && amiga_local_ms()>=G.status_until){G.status_until=0;mark_dirty(DIRTY_HEADER);}
    draft_checkpoint(); if(G.multi) { int i; for(i=0;i<PREFS_SERVERS_MAX;i++) if(G.servers[i].cooling || G.servers[i].health_retry) { rooms_rebuild(); mark_dirty(DIRTY_HEADER); break; } }
    G.tick->tr_node.io_Command=TR_ADDREQUEST; G.tick->tr_time.tv_secs=2; G.tick->tr_time.tv_micro=0;
    SendIO((struct IORequest *)G.tick); G.tick_pending=1;
}
static void draft_start(void)
{
    if((!strcmp(draft_path,"PROGDIR:AmiRocket.drafts") || draft_path==installed_draft_path) && prefs_resource_path(&G.prefs,"PROGDIR:AmiRocket.drafts",installed_draft_path,sizeof(installed_draft_path))==0)draft_path=installed_draft_path;
    G.drafts=ar_calloc(1,sizeof(*G.drafts));
    if(G.drafts && prefs_lock()==0) {
        int rc=draft_read(G.drafts); prefs_unlock();
        if(rc<0) { server_notice(G.active_server,"Draft recovery file is unreadable; existing data will not be overwritten"); G.draft_warned=1; }
    }
    G.tick_port=CreateMsgPort();
    if(G.tick_port) G.tick=(struct timerequest *)CreateIORequest(G.tick_port,sizeof(struct timerequest));
    if(G.tick && !OpenDevice((CONST_STRPTR)TIMERNAME,UNIT_VBLANK,(struct IORequest *)G.tick,0)) {
        G.tick_open=1; G.tick->tr_node.io_Command=TR_ADDREQUEST; G.tick->tr_time.tv_secs=2;
        SendIO((struct IORequest *)G.tick); G.tick_pending=1;
    }
}
static void draft_stop(void)
{
    draft_checkpoint();
    if(G.tick_pending) { if(!CheckIO((struct IORequest *)G.tick)) AbortIO((struct IORequest *)G.tick); WaitIO((struct IORequest *)G.tick); }
    if(G.tick_open) CloseDevice((struct IORequest *)G.tick);
    if(G.tick) DeleteIORequest((struct IORequest *)G.tick);
    if(G.tick_port) DeleteMsgPort(G.tick_port);
    G.tick=NULL; G.tick_port=NULL; G.tick_open=G.tick_pending=0;
    if(G.drafts) { drafts_free(G.drafts); ar_free(G.drafts); G.drafts=NULL; }
}

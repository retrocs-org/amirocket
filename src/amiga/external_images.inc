/* Originals are downloaded without decoding them in the client. Keep temporary
 * files while the asynchronous viewer may still be using them. */
static long image_file_size(const char *path);
static int image_viewer_ready(void)
{
    char text[420];
    if(G.prefs.image_viewer[0] && (!strchr(G.prefs.image_viewer,':') || image_file_size(G.prefs.image_viewer)>0))return 1;
    snprintf(text,sizeof(text),"Image viewer executable not found: %s\n\nChoose an installed viewer in AmiRocketPrefs / Helpers.",G.prefs.image_viewer);
    request("Image viewer",text);return 0;
}
static int image_viewer_command(Buf *cmd,const char *path)
{
    if(!path || strpbrk(path,"`$") || strpbrk(G.prefs.image_viewer,"`$")) return 0;
    if(!shell_argument(cmd,G.prefs.image_viewer) || (buf_putc(cmd,' '),!shell_argument(cmd,path)))return 0;
    if(!ar_strcasecmp(FilePart(G.prefs.image_viewer),"MultiView"))buf_puts(cmd," PUBSCREEN Workbench WINDOW");
    return !cmd->oom;
}
static int launch_image_viewer(const char *path)
{
    Buf cmd; LONG rc;if(!image_viewer_ready())return 0;buf_init(&cmd);
    if(!image_viewer_command(&cmd,path)) { buf_free(&cmd); request("Image viewer","Configure a valid image-viewer executable in Preferences / Helpers."); return 0; }
    set_status("Opening image viewer...");
    rc=launch_helper(buf_str(&cmd)); buf_free(&cmd);
    if(rc>=0 && !ar_strcasecmp(FilePart(G.prefs.image_viewer),"MultiView")) {
        struct Screen *workbench=LockPubScreen((CONST_STRPTR)"Workbench");
        if(workbench){ScreenToFront(workbench);UnlockPubScreen(NULL,workbench);}
    }
    if(rc<0) request("Image viewer","Could not start the configured viewer. The downloaded file has been retained.");
    if(rc>=0)set_status("Image handed to external viewer");
    return rc>=0;
}
static long image_file_size(const char *path)
{
    BPTR lock=Lock(path,ACCESS_READ); struct FileInfoBlock *fib; long size=-1;
    if(!lock) return -1;
    fib=AllocDosObject(DOS_FIB,NULL);
    if(fib) { if(Examine(lock,fib) && fib->fib_DirEntryType<0) size=fib->fib_Size; FreeDosObject(DOS_FIB,fib); }
    UnLock(lock); return size;
}
static void external_image_forget(int index,int remove)
{
    if(remove) { if(G.external_images[index].path[0]) DeleteFile(G.external_images[index].path);
      if(G.external_images[index].drawer[0]) DeleteFile(G.external_images[index].drawer); }
    ar_free(G.external_images[index].url); memset(&G.external_images[index],0,sizeof(G.external_images[index]));
}
static int external_images_clear(void)
{
    int i,ok=1;
    struct EasyStruct es={sizeof(es),0,(STRPTR)"Temporary image files",(STRPTR)"Close external image viewers before clearing their temporary files.",(STRPTR)"Clear files|Cancel"};
    if(EasyRequest(G.win,&es,NULL)!=1) return 0;
    for(i=0;i<4;i++) if(G.external_images[i].url) {
        if(!DeleteFile(G.external_images[i].path) && IoErr()!=ERROR_OBJECT_NOT_FOUND) { ok=0; continue; }
        external_image_forget(i,1);
    }
    if(!ok) request("Temporary images","Some files are still in use. Close the viewer and try again.");
    return ok;
}
static void open_external_image(int message,int file)
{
    RcAttachment *a; NetMsg *m; int i,slot=-1,n=0; size_t used=0,limit=G.prefs.low_memory?2UL*1024*1024:8UL*1024*1024;
    char name[96],base[280]; Buf title; BPTR lock;
    if(G.download_pending || message<0 || message>=G.msgs.count || file<0 || file>=G.msgs.items[message].file_count) return;
    a=&G.msgs.items[message].files[file]; if(!a->image || !a->url) return;
    if(view_attachment_limit(a,0)) { request("Image viewer",view_attachment_limit(a,0)); return; }
    if(a->size>0 && (unsigned long long)a->size>limit) { request("Image viewer","This original exceeds the temporary-image budget. Use Save attachment or disable low-memory mode."); return; }
    if(!image_viewer_ready())return;
    for(i=0;i<4;i++) {
        long bytes=G.external_images[i].url?image_file_size(G.external_images[i].path):-1;
        if(!G.external_images[i].url) { if(slot<0) slot=i; continue; }
        if(bytes>=0 && G.external_images[i].slot==G.active_server && G.external_images[i].generation==G.servers[G.active_server].generation && !strcmp(G.external_images[i].url,a->url)) { launch_image_viewer(G.external_images[i].path); return; }
        used+=bytes<0?limit:(size_t)bytes;
    }
    if(slot<0 || used>=limit || (a->size>0 && (unsigned long long)a->size>limit-used)) {
        if(external_images_clear()) open_external_image(message,file);
        return;
    }
    buf_init(&title); to_latin1(&title,a->title,0);
    for(i=0;buf_str(&title)[i] && n<(int)sizeof(name)-1;i++) { unsigned char c=buf_str(&title)[i];
      if((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || strchr(" ._-",c)) name[n++]=c; }
    name[n]=0; buf_free(&title); if(!name[0] || !strcmp(name,".") || !strcmp(name,"..")) strcpy(name,"image");
    lock=CreateDir(G.prefs.cache_dir);if(lock)UnLock(lock);
    ar_strlcpy(base,G.prefs.cache_dir,sizeof(base));if(!AddPart(base,"Originals",sizeof(base)))return;
    lock=CreateDir(base);if(lock)UnLock(lock);
    for(i=0,lock=0;i<16 && !lock;i++) {
        G.external_serial++; if(!G.external_serial) G.external_serial++;
        snprintf(G.external_images[slot].drawer,sizeof(G.external_images[slot].drawer),"%s/AmiRocket-view-%lx-%lu",base,(unsigned long)FindTask(NULL),(unsigned long)G.external_serial);
        lock=CreateDir(G.external_images[slot].drawer);
    }
    if(!lock) { request("Image viewer","Cannot create a temporary image drawer."); return; } UnLock(lock);
    ar_strlcpy(G.external_images[slot].path,G.external_images[slot].drawer,sizeof(G.external_images[slot].path));
    if(!AddPart(G.external_images[slot].path,name,sizeof(G.external_images[slot].path))) { external_image_forget(slot,1); return; }
    G.external_images[slot].url=ar_strdup(a->url); G.external_images[slot].slot=G.active_server; G.external_images[slot].generation=G.servers[G.active_server].generation;
    m=netmsg_new(NC_DOWNLOAD);
    if(!m || !G.external_images[slot].url) { netmsg_free(m); external_image_forget(slot,1); return; }
    m->a_url=ar_strdup(a->url); m->a_text=ar_strdup(G.external_images[slot].path); m->a_verify=2; m->a_before=limit-used;
    if(!m->a_url || !m->a_text) { netmsg_free(m); external_image_forget(slot,1); return; }
    G.external_pending=slot+1; transfer_dialog(m);
    if(G.external_pending==slot+1) { G.external_pending=0; external_image_forget(slot,1); }
}

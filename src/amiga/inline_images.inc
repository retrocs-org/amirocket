/* Lazy inline image cache: eight attachments, 8 MiB encoded data total. */
static void images_drop_pictures(void)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (G.images[i].picture) DisposeDTObject(G.images[i].picture);
        G.images[i].picture = NULL; G.images[i].bitmap = NULL;
        if (G.images[i].path[0]) DeleteFile(G.images[i].path);
        G.images[i].path[0] = 0;
    }
}
static void images_reset(void)
{
    int i; images_drop_pictures();
    for (i = 0; i < 8; i++) { ar_free(G.images[i].url); buf_free(&G.images[i].bytes); }
    memset(G.images, 0, sizeof(G.images)); G.image_generation++;
}
static void image_release(int i)
{
    if(G.images[i].picture)DisposeDTObject(G.images[i].picture);
    if(G.images[i].path[0])DeleteFile(G.images[i].path);
    ar_free(G.images[i].url);buf_free(&G.images[i].bytes);
    memset(&G.images[i],0,sizeof(G.images[i]));
}
static int image_decode(int i)
{
    struct BitMapHeader *header = NULL;
    Object *pic; BPTR f; LONG wrote; int width, height, maxw = G.prefs.low_memory?160:G.prefs.preview_width, maxh;
    if(maxw!=160 && maxw!=320 && maxw!=640) maxw=320;
    maxh=maxw*9/16;
    if (G.images[i].bitmap) return 1;
    if (!G.images[i].bytes.len || !DataTypesBase ||
        !picture_dimensions_ok((unsigned char *)G.images[i].bytes.data, G.images[i].bytes.len, G.prefs.low_memory?1024:2048, G.prefs.low_memory?250000:1000000)) return 0;
    if (G.chat.width > 0 && G.chat.width - G.chat.line_h * 2 - 20 < maxw)
        maxw = G.chat.width - G.chat.line_h * 2 - 20;
    if (maxw < 64) maxw = 64;
    { char drawer[280];BPTR root=CreateDir(G.prefs.cache_dir);if(root)UnLock(root);
      ar_strlcpy(drawer,G.prefs.cache_dir,sizeof(drawer));if(!AddPart(drawer,"Thumbnails",sizeof(drawer)))return 0;root=CreateDir(drawer);if(root)UnLock(root);
      snprintf(G.images[i].path,sizeof(G.images[i].path),"%s/AmiRocket-%lx-%d.image",drawer,(unsigned long)FindTask(NULL),i); }
    f = Open(G.images[i].path, MODE_NEWFILE); if (!f) return 0;
    wrote = Write(f, G.images[i].bytes.data, G.images[i].bytes.len); Close(f);
    if (wrote != (LONG)G.images[i].bytes.len) return 0;
    pic = NewDTObject((APTR)G.images[i].path, DTA_SourceType, DTST_FILE, DTA_GroupID, GID_PICTURE,
        PDTA_DestMode, PMODE_V43, PDTA_FreeSourceBitMap, TRUE, PDTA_Remap, TRUE, PDTA_Screen, (ULONG)G.scr, TAG_END);
    if (!pic) return 0;
    GetDTAttrs(pic, PDTA_BitMapHeader, (ULONG)&header, TAG_END);
    if (!header || !header->bmh_Width || !header->bmh_Height) { DisposeDTObject(pic); return 0; }
    width = header->bmh_Width; height = header->bmh_Height;
    if (width > maxw) { height = (LONG)height * maxw / width; width = maxw; }
    if (height > maxh) { width = (LONG)width * maxh / height; height = maxh; }
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (!DoMethod(pic, PDTM_SCALE, width, height, 0) || !DoDTMethod(pic, NULL, NULL, DTM_PROCLAYOUT, NULL, TRUE)) { DisposeDTObject(pic); return 0; }
    GetDTAttrs(pic, PDTA_DestBitMap, (ULONG)&G.images[i].bitmap, TAG_END);
    if (!G.images[i].bitmap || GetBitMapAttr(G.images[i].bitmap, BMA_WIDTH) < (ULONG)width ||
        GetBitMapAttr(G.images[i].bitmap, BMA_HEIGHT) < (ULONG)height) {
        G.images[i].bitmap = NULL; DisposeDTObject(pic); return 0;
    }
    G.images[i].picture = pic; G.images[i].width = width; G.images[i].height = height;
    return 1;
}
static void image_loading_done(const char *url)
{
    int i; if(!url) return;
    for(i=0;i<8;i++) if(G.images[i].url && !strcmp(G.images[i].url,url)) G.images[i].loading=0;
}
static void image_save_offer(int message,int file,const char *why)
{
    struct EasyStruct es={sizeof(es),0,(STRPTR)"Image preview",(STRPTR)"%s",(STRPTR)"Save attachment...|Close"};
    int answer; es.es_GadgetFormat=(STRPTR)"Open original externally|Save attachment...|Close";
    answer=EasyRequest(G.win,&es,NULL,(ULONG)why);
    if(answer==1) open_external_image(message,file);
    else if(answer==2) download_attachment(message,file);
}
static void image_toggle(int message, int file)
{
    RcAttachment *a; const char *preview; NetMsg *m; int i;
    if (!G.prefs.inline_images) { if(G.prefs.external_images) open_external_image(message,file); else download_attachment(message,file); return; }
    if (message < 0 || message >= G.msgs.count || file < 0 || file >= G.msgs.items[message].file_count) return;
    a = &G.msgs.items[message].files[file];
    if (!a->image || !a->url) return;
    if(G.prefs.external_images && (!a->preview_url || !*a->preview_url || !strcmp(a->preview_url,a->url))) { open_external_image(message,file); return; }
    if(view_attachment_limit(a,1)) { image_save_offer(message,file,view_attachment_limit(a,1)); return; }
    preview=rc_attachment_preview_url(a);
    for (i = 0; i < 8; i++) if (G.images[i].url && !strcmp(G.images[i].url, preview)) break;
    if (i == 8) {
        for (i = 0; i < 8 && G.images[i].url; i++);
        if (i == 8 || (G.prefs.low_memory && i>=2)) {
            int j,victim=-1,limit=G.prefs.low_memory?2:8;
            for(j=0;j<limit;j++)if(!G.images[j].loading && !G.images[j].expanded && (victim<0 || G.images[j].touched<G.images[victim].touched))victim=j;
            if(victim<0) {set_status("Thumbnail cache full - hide a preview or clear media caches");return;}
            image_release(victim);i=victim;
        }
        G.images[i].url = ar_strdup(preview); buf_init(&G.images[i].bytes);
        if (!G.images[i].url) return;
    }
    G.images[i].touched=++G.image_clock;
    if (G.images[i].expanded) { G.images[i].expanded = 0; msgs_rebuild(0); return; }
    if (G.images[i].bytes.len) {
        if (image_decode(i)) { G.images[i].expanded = 1; remember_anchor(); msgs_rebuild(0); }
        else { G.images[i].failed=1; set_status("Preview unavailable - use Open or Save"); msgs_rebuild(0); }
        return;
    }
    if (G.images[i].loading) return;
    m = netmsg_new(NC_IMAGE); if (!m) return;
    m->a_url = ar_strdup(preview); m->a_interval = G.image_generation;
    m->a_before=memory_image_limit()<2UL*1024*1024?memory_image_limit():2UL*1024*1024;
    if (!m->a_url) { netmsg_free(m); return; }
    G.images[i].loading = 1; G.images[i].failed=0; send_cmd(m); msgs_rebuild(0);
}
static void on_image(NetMsg *m)
{
    int i; size_t total = 0;
    if(G.transfer_dialog && m->sent_at==G.transfer_serial) {
        G.download_pending=0; G.transfer_rc=m->rc;
        ar_strlcpy(G.transfer_status,m->text[0]?m->text:rc_strerror(m->rc),sizeof(G.transfer_status));
    }
    if (m->a_interval != G.image_generation || !G.prefs.inline_images) return;
    for (i = 0; i < 8; i++) total += G.images[i].bytes.len;
    for (i = 0; i < 8; i++) if (G.images[i].url && m->a_url && !strcmp(G.images[i].url, m->a_url)) {
        G.images[i].loading = 0;
        if (m->rc != RC_OK) { G.images[i].failed=1; set_status_utf8(m->text[0] ? m->text : "Image download failed - Retry is available on the attachment"); msgs_rebuild(0); return; }
        if (total + m->avatar.len > memory_image_limit()) { G.images[i].failed=1; msgs_rebuild(0); G.transfer_rc=RC_ERR_ARGS; G.transfer_save_offer=1; ar_strlcpy(G.transfer_status,"Image memory limit reached. Clear previews or adjust Preferences / Memory.",sizeof(G.transfer_status)); return; }
        G.images[i].bytes = m->avatar; buf_init(&m->avatar);
        if (image_decode(i)) { G.images[i].expanded = 1; remember_anchor(); msgs_rebuild(0); set_status("Thumbnail ready - Open uses the external viewer; Hide collapses the preview"); }
        else { G.images[i].failed=1; buf_free(&G.images[i].bytes); buf_init(&G.images[i].bytes); G.transfer_save_offer=1; G.transfer_rc=RC_ERR_ARGS; ar_strlcpy(G.transfer_status,"Cannot preview this image within memory limits. Enable smaller server thumbnails or use Open / Save.",sizeof(G.transfer_status)); msgs_rebuild(0); }
        return;
    }
}
static void image_view_update(void)
{
    int i, m, f;
    G.chat.nimages = 0;
    if (!G.prefs.inline_images) return;
    for (i = 0; i < 8; i++) if (G.images[i].url) {
        for (m = 0; m < G.msgs.count; m++) for (f = 0; f < G.msgs.items[m].file_count; f++) {
            const char *url = rc_attachment_preview_url(&G.msgs.items[m].files[f]);
            if (url && !strcmp(url, G.images[i].url) && G.chat.nimages < 8) {
                ChatImage *image = &G.chat.images[G.chat.nimages++];
                image->msg_index = m; image->file_index = f; image->bitmap = G.images[i].bitmap;
                image->state=G.images[i].loading?1:G.images[i].failed?2:G.images[i].expanded?3:0;
                image->width = G.images[i].expanded?G.images[i].width:0; image->height = G.images[i].expanded?G.images[i].height:0;
            }
        }
    }
}

/* At most one visible, server-provided thumbnail is queued at a time.
 * Never fetch originals automatically or retry failures in a redraw loop. */
static void images_auto_visible(void)
{
    int row,i,message,file;const RcAttachment *a;const char *url;
    if(!G.prefs.inline_images || !G.prefs.auto_thumbnails || G.prefs.low_memory || !G.connected || G.server_view || G.iconified || G.n_sugg)return;
    for(i=0;i<8;i++)if(G.images[i].loading)return;
    for(row=G.chat.top;row<G.chat.n && row<G.chat.top+G.chat.visible;row++) {
        if(row<0 || G.chat.v[row].kind!=VL_FILE)continue;
        message=G.chat.v[row].msg_index;file=G.chat.v[row].file_index;
        if(message<0 || message>=G.msgs.count || file<0 || file>=G.msgs.items[message].file_count)continue;
        a=&G.msgs.items[message].files[file];url=rc_attachment_preview_url(a);
        if(!a->image || !a->preview_url || !*a->preview_url || !url || !strcmp(url,a->url) || view_attachment_limit(a,1))continue;
        for(i=0;i<8;i++)if(G.images[i].url && !strcmp(G.images[i].url,url))break;
        if(i<8)continue;
        for(i=0;i<8 && G.images[i].url;i++);
        if(i==8)return;
        image_toggle(message,file);return;
    }
}

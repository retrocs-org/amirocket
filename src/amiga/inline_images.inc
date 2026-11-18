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
static int image_decode(int i)
{
    struct BitMapHeader *header = NULL;
    Object *pic; BPTR f; LONG wrote; int width, height, maxw = 320, maxh = 180;
    if (G.images[i].bitmap) return 1;
    if (!G.images[i].bytes.len || !DataTypesBase ||
        !picture_dimensions_ok((unsigned char *)G.images[i].bytes.data, G.images[i].bytes.len, 2048, 1000000)) return 0;
    if (G.chat.width > 0 && G.chat.width - G.chat.line_h * 2 - 20 < maxw)
        maxw = G.chat.width - G.chat.line_h * 2 - 20;
    if (maxw < 64) maxw = 64;
    snprintf(G.images[i].path, sizeof(G.images[i].path), "T:AmiRocket-image-%lx-%d.image", (unsigned long)FindTask(NULL), i);
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
    if(EasyRequest(G.win,&es,NULL,(ULONG)why)==1) download_attachment(message,file);
}
static void image_toggle(int message, int file)
{
    RcAttachment *a; NetMsg *m; int i;
    if (!G.prefs.inline_images) { download_attachment(message,file); return; }
    if (message < 0 || message >= G.msgs.count || file < 0 || file >= G.msgs.items[message].file_count) return;
    a = &G.msgs.items[message].files[file];
    if (!a->image || !a->url) return;
    if(view_attachment_limit(a,0)) { request("Attachment too large",view_attachment_limit(a,0)); return; }
    if(view_attachment_limit(a,1)) { image_save_offer(message,file,view_attachment_limit(a,1)); return; }
    for (i = 0; i < 8; i++) if (G.images[i].url && !strcmp(G.images[i].url, a->url)) break;
    if (i == 8) {
        for (i = 0; i < 8 && G.images[i].url; i++);
        if (i == 8) { set_status("Image cache full - use Chat / Clear image previews"); return; }
        G.images[i].url = ar_strdup(a->url); buf_init(&G.images[i].bytes);
        if (!G.images[i].url) return;
    }
    if (G.images[i].expanded) { G.images[i].expanded = 0; msgs_rebuild(0); return; }
    if (G.images[i].bytes.len) {
        if (image_decode(i)) { G.images[i].expanded = 1; remember_anchor(); msgs_rebuild(0); }
        else image_save_offer(message,file,"No suitable picture datatype is installed, or this image exceeds the preview limits. Save it to open externally.");
        return;
    }
    if (G.images[i].loading) return;
    m = netmsg_new(NC_IMAGE); if (!m) return;
    m->a_url = ar_strdup(a->url); m->a_interval = G.image_generation;
    if (!m->a_url) { netmsg_free(m); return; }
    G.images[i].loading = 1; transfer_dialog(m);
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
        if (m->rc != RC_OK) { set_status_utf8(m->text[0] ? m->text : "Image download failed"); if(!G.transfer_dialog) request("Image preview",m->text[0] ? m->text : "Image download failed. Click the image label to retry."); return; }
        if (total + m->avatar.len > 8UL * 1024UL * 1024UL) { G.transfer_rc=RC_ERR_ARGS; ar_strlcpy(G.transfer_status,"Image cache limit reached (8 MiB). Clear previews and retry.",sizeof(G.transfer_status)); return; }
        G.images[i].bytes = m->avatar; buf_init(&m->avatar);
        if (image_decode(i)) { G.images[i].expanded = 1; remember_anchor(); msgs_rebuild(0); set_status("Image expanded - click its label to collapse"); }
        else { buf_free(&G.images[i].bytes); buf_init(&G.images[i].bytes); G.transfer_save_offer=1; G.transfer_rc=RC_ERR_ARGS; ar_strlcpy(G.transfer_status,"Cannot preview this format or image exceeds 1 million pixels. Use Save attachment.",sizeof(G.transfer_status)); }
        return;
    }
}
static void image_view_update(void)
{
    int i, m, f;
    G.chat.nimages = 0;
    if (!G.prefs.inline_images) return;
    for (i = 0; i < 8; i++) if (G.images[i].expanded && image_decode(i)) {
        for (m = 0; m < G.msgs.count; m++) for (f = 0; f < G.msgs.items[m].file_count; f++) {
            const char *url = G.msgs.items[m].files[f].url;
            if (url && !strcmp(url, G.images[i].url) && G.chat.nimages < 8) {
                ChatImage *image = &G.chat.images[G.chat.nimages++];
                image->msg_index = m; image->file_index = f; image->bitmap = G.images[i].bitmap;
                image->width = G.images[i].width; image->height = G.images[i].height;
            }
        }
    }
}

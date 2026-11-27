static void qa_unread_attachments(void)
{
    RcRoom saved[4]; char anchor[96],marker[64],size[48],rid[64]; int i,slot,seen=0,connected=G.connected;
    ar_ms boundary=G.read_boundary; int pending=G.boundary_unread,inline_images=G.prefs.inline_images,avatars=G.prefs.show_avatars;
    qa_trace("QA unread badges, last-read boundary and next unread target\n");
    memcpy(saved,G.rooms.items,sizeof(saved)); ar_strlcpy(anchor,G.unread_anchor,sizeof(anchor)); ar_strlcpy(marker,G.marker_room,sizeof(marker));
    G.prefs.show_avatars=0; /* No network worker in this synthetic view fixture. */
    G.connected=1; G.server_view=0; strcpy(G.cur_rid,"r1");
    G.rooms.items[0].unread=3; G.rooms.items[0].mentions=2; G.rooms.items[0].last_seen_ms=G.msgs.items[55].ts_ms;
    unread_capture("r1"); G.rooms.items[0].unread=G.rooms.items[0].mentions=0;
    unread_resolve(); QA(!strcmp(G.unread_anchor,G.msgs.items[56].id));
    msgs_rebuild(0); redraw_dirty();
    for(i=0;i<G.chat.n;i++) if(G.chat.v[i].kind==VL_UNREAD) seen++;
    QA(seen==1);
    G.rooms.items[0].unread=2; G.rooms.items[0].last_seen_ms=0; unread_capture("r1"); unread_resolve();
    QA(!strcmp(G.unread_anchor,G.msgs.items[G.msgs.count-2].id));
    { int seek=G.unread_seek,exhausted=G.history_exhausted,count=G.msgs.count;ar_ms read=G.read_boundary;int unread=G.boundary_unread;
      G.history_exhausted=0;G.read_boundary=G.msgs.items[0].ts_ms-1;G.boundary_unread=500;
      G.unread_seek=0;QA(!unread_needs_history()); /* Opening a room never scans history. */
      G.unread_seek=1;QA(unread_needs_history());
      G.history_exhausted=1;QA(!unread_needs_history());G.history_exhausted=0;
      G.msgs.count=MAX_MESSAGES;QA(!unread_needs_history());G.msgs.count=count;
      G.read_boundary=0;QA(unread_needs_history()); /* Older servers without ls. */
      G.unread_seek=seek;G.history_exhausted=exhausted;G.read_boundary=read;G.boundary_unread=unread;
    }
    G.rooms.items[0].unread=G.rooms.items[0].alert=0; G.rooms.items[1].unread=0; G.rooms.items[1].alert=0; G.rooms.items[1].mentions=2;
    rooms_rebuild();
    for(i=0,seen=0;i<G.side.n;i++) if(G.side.r[i].rid && !strcmp(G.side.r[i].rid,"r2")) { QA(G.side.r[i].bold && G.side.r[i].mentions==2); seen=1; }
    QA(seen && unread_target(&slot,rid,sizeof(rid)) && !strcmp(rid,"r2"));
    G.rooms.items[0].unread=1; strcpy(G.cur_rid,"r3");
    QA(unread_target(&slot,rid,sizeof(rid)) && !strcmp(rid,"r1")); strcpy(G.cur_rid,"r1");
    G.connected=0; QA(!unread_target(&slot,rid,sizeof(rid))); G.connected=1;
    qa_trace("QA attachment sizes, limits and unavailable image datatypes\n");
    view_file_size(size,sizeof(size),0); QA(!strcmp(size,"size unknown"));
    view_file_size(size,sizeof(size),1536); QA(!strcmp(size,"1.5 KiB"));
    { RcAttachment file={0}; file.size=3LL*1024*1024; QA(view_attachment_limit(&file,1) && !view_attachment_limit(&file,0));
      file.size=9LL*1024*1024; QA(view_attachment_limit(&file,0)!=NULL);
      file.image=1; file.url="/original"; file.preview_url="/thumbnail"; QA(view_attachment_limit(&file,1)==NULL);
      file.preview_url="/original"; QA(view_attachment_limit(&file,1)!=NULL);

    }
    { RcAttachment *file=NULL; long long old_size=0;
      for(i=0;i<G.msgs.count;i++) if(G.msgs.items[i].file_count) { file=&G.msgs.items[i].files[0]; break; }
      QA(file!=NULL); if(file) { old_size=file->size; file->size=2048;
          msgs_rebuild(0); redraw_dirty(); QA(strstr(buf_str(&G.chat.pool),"2.0 KiB")!=NULL); file->size=old_size; }
    }
    { NetMsg *image=netmsg_new(NC_IMAGE); QA(image!=NULL); if(image) {
        images_reset(); G.images[0].url=ar_strdup("/qa/unsupported"); G.images[0].loading=1;
        image->a_url=ar_strdup("/qa/unsupported"); image->a_interval=G.image_generation; image->sent_at=999;
        buf_puts(&image->avatar,"not an image"); G.transfer_dialog=1; G.transfer_serial=999; G.download_pending=1; G.transfer_save_offer=0;
        G.prefs.inline_images=1; on_image(image);
        QA(!G.images[0].loading && !G.download_pending && G.transfer_save_offer && G.transfer_rc!=RC_OK && !G.images[0].expanded);
        QA(strstr(G.transfer_status,"Open / Save")!=NULL);
        G.transfer_dialog=0; G.transfer_save_offer=0; G.transfer_serial=0; netmsg_free(image); images_reset();
    } }
    memcpy(G.rooms.items,saved,sizeof(saved)); G.connected=connected;
    ar_strlcpy(G.unread_anchor,anchor,sizeof(G.unread_anchor)); ar_strlcpy(G.marker_room,marker,sizeof(G.marker_room));
    G.prefs.inline_images=inline_images; G.prefs.show_avatars=avatars;
    G.read_boundary=boundary; G.boundary_unread=pending; rooms_rebuild(); msgs_rebuild(0);
}

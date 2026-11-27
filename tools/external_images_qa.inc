static void qa_external_images(void)
{
    char viewer[128],result[300]={0}; int external=G.prefs.external_images,i,message=-1,pending=G.pending;
    Buf command; BPTR f; RcAttachment *attachment; NetMsg done={0};
    qa_trace("QA external originals, literal file arguments and cache reuse\n");
    for(i=0;i<G.msgs.count;i++) if(G.msgs.items[i].file_count && G.msgs.items[i].files[0].image) { message=i; break; }
    QA(message>=0); if(message<0) return; attachment=&G.msgs.items[message].files[0];
    ar_strlcpy(viewer,G.prefs.image_viewer,sizeof(viewer));
    strcpy(G.prefs.image_viewer,"AmiRocket:AmiRocketViewerQA"); G.prefs.external_images=1;
    buf_init(&command); QA(image_viewer_command(&command,"T:picture name*\".png"));
    QA(strstr(buf_str(&command),"picture name***\".png")!=NULL); buf_free(&command);
    buf_init(&command); QA(!image_viewer_command(&command,"T:bad\nname.png")); buf_free(&command);
    buf_init(&command); QA(!image_viewer_command(&command,"T:`echo bad`.png")); buf_free(&command);
    f=Open("T:AmiRocket-viewer-qa-source.png",MODE_NEWFILE); QA(f!=0);
    if(f) {
        Write(f,"not decoded by the client",25); Close(f);
        G.external_images[0].url=ar_strdup(attachment->url); G.external_images[0].slot=G.active_server;
        QA(G.external_images[0].url!=NULL);
        G.external_images[0].generation=G.servers[G.active_server].generation;
        strcpy(G.external_images[0].path,"T:AmiRocket-viewer-qa-source.png");
        DeleteFile("T:AmiRocket-viewer-qa.receipt");
        open_external_image(message,0);
        for(i=0;i<100;i++) { f=Open("T:AmiRocket-viewer-qa.receipt",MODE_OLDFILE); if(f) break; Delay(2); }
        QA(f!=0); if(f) { Read(f,result,sizeof(result)-1); Close(f); }
        QA(!strcmp(result,"T:AmiRocket-viewer-qa-source.png") && G.pending==pending);
        QA(image_file_size(G.external_images[0].path)==25);
        DeleteFile("T:AmiRocket-viewer-qa.receipt");
        done.rc=RC_OK; done.a_verify=2; done.a_text=G.external_images[0].path; G.external_pending=1;
        on_downloaded(&done); QA(!G.external_pending);
        for(i=0;i<100;i++) { f=Open("T:AmiRocket-viewer-qa.receipt",MODE_OLDFILE); if(f) break; Delay(2); }
        QA(f!=0); if(f) Close(f);
        QA(image_file_size(done.a_text)==25); /* async viewer retains its input */
        external_image_forget(0,1); DeleteFile("T:AmiRocket-viewer-qa.receipt");
    }
    { char value[16]; QA(prefs_get(&G.prefs,"external_images",value,sizeof(value))==0 && !strcmp(value,"1"));
      QA(prefs_set(&G.prefs,"external_images","0")==0 && !G.prefs.external_images); }
    { Buf command;char previous[128];ar_strlcpy(previous,G.prefs.image_viewer,sizeof(previous));
      ar_strlcpy(G.prefs.image_viewer,"SYS:Utilities/MultiView",sizeof(G.prefs.image_viewer));
      buf_init(&command);QA(image_viewer_command(&command,"T:picture.png") && strstr(buf_str(&command),"PUBSCREEN Workbench WINDOW"));buf_free(&command);
      ar_strlcpy(G.prefs.image_viewer,previous,sizeof(G.prefs.image_viewer));
    }
    G.prefs.external_images=external; ar_strlcpy(G.prefs.image_viewer,viewer,sizeof(G.prefs.image_viewer));
}

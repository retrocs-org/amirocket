/* Test the actual worker state machine without a live account or network. */
#define AR_WORKER_QA 1
#include "../src/amiga/worker.c"
#include "../tests/download_fixture.h"
static void qa_cancel_download(void *ctx) {worker_cancel_transfer(0,*(ULONG *)ctx);}
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>
extern struct Library *SocketBase;
static int qa_stall_cancel(void *ctx) {return network_now()-*(ar_ms *)ctx>=250;}
int worker_recovery_qa(int *passed)
{
    Worker w={0}; struct MsgPort *saved=g_guiport,*events=CreateMsgPort(); NetMsg *e; int fail=0,i;
#define WQA(x) do { if(x) (*passed)++; else fail++; } while(0)
    if(!events) return 1;
    w.net_trouble=1;strcpy(w.sess.rc.http.err,"Cannot resolve secret.example?token=PRIVATE");
    WQA(!strcmp(network_diagnosis(&w),"DNS lookup failed"));
    strcpy(w.sess.rc.http.err,"SSL certificate error with PRIVATE");WQA(!strcmp(network_diagnosis(&w),"TLS/certificate failure"));
    w.traffic.last_status=429;WQA(strstr(network_diagnosis(&w),"rate limit")!=NULL);
    w.traffic.last_status=401;WQA(strstr(network_diagnosis(&w),"Authentication")!=NULL);
    w.traffic.last_status=502;WQA(strstr(network_diagnosis(&w),"reverse-proxy")!=NULL);
    memset(&w,0,sizeof(w));
    /* A command wake between timer ticks must return to sleep, not feed an
     * AbortIO reply back into Wait. Exercise real timer.device signals. */
    { Worker clock={0}; int tick;
      clock.tport=CreateMsgPort();
      if(clock.tport) clock.treq=(struct timerequest *)CreateIORequest(clock.tport,sizeof(struct timerequest));
      if(clock.treq && !OpenDevice((CONST_STRPTR)TIMERNAME,UNIT_VBLANK,(struct IORequest *)clock.treq,0)) clock.timer_open=1;
      WQA(clock.timer_open);
      if(clock.timer_open) for(tick=0;tick<3;tick++) {
        ULONG bit=1UL<<clock.tport->mp_SigBit; ar_ms began=network_now();
        timer_keep_wake(&clock);
        Signal(FindTask(NULL),bit); Wait(bit); /* unrelated wake */
        timer_keep_wake(&clock);
        Wait(bit);
        WQA(CheckIO((struct IORequest *)clock.treq)!=NULL);
        WQA(network_now()-began>=700);
        timer_stop(&clock);
      }
      worker_release(&clock);
    }
    g_guiport=events; w.evreply=events; w.slot=1; w.generation=7;
    send_failure(&w,RC_ERR_NET,"DNS failed"); e=(NetMsg *)GetMsg(events);
    WQA(w.retry_delay==5 && w.net_trouble && e && e->a_interval==5 && e->server_slot==1);
    if(e) netmsg_free(e);
    for(i=0;i<10;i++) { schedule_retry(&w,RC_ERR_HTTP,"Server unavailable"); e=(NetMsg *)GetMsg(events); if(e) netmsg_free(e); }
    WQA(w.retry_delay==300);
    w.sess.rc.last_status=503; WQA(retryable_failure(&w,RC_ERR_SERVER));
    w.sess.rc.last_status=429; WQA(retryable_failure(&w,RC_ERR_SERVER));
    w.sess.rc.last_status=403; WQA(!retryable_failure(&w,RC_ERR_SERVER));
    w.sess.logged_in=1; w.retry_login=1; strcpy(w.retry_token,"expired");
    send_failure(&w,RC_ERR_AUTH,"Unauthorized"); e=(NetMsg *)GetMsg(events);
    WQA(!w.sess.logged_in && !w.retry_login && !w.retry_delay && !w.retry_token[0]);
    WQA(e && e->rc==RC_ERR_AUTH && !e->a_interval && strstr(e->text,"Draft kept"));
    if(e) netmsg_free(e);
    w.outstanding=0;
    { TransferProgress progress={&w,123,0,0};
      transfer_cancel[w.slot]=123; WQA(transfer_progress(&progress,100,200)==1);
      progress.serial=124; WQA(transfer_progress(&progress,100,200)==0);
      e=(NetMsg *)GetMsg(events); WQA(e && e->kind==NE_TRANSFER && e->sent_at==124 && e->a_before==100 && e->a_interval==200);
      if(e) netmsg_free(e);
      transfer_cancel[w.slot]=0;
    }
    /* GUI acknowledgements must free the three-event budget DURING a long
     * transfer, rather than waiting for the outer worker loop. */
    { struct MsgPort *replies=CreateMsgPort(); TransferProgress progress={&w,321,0,0};
      WQA(replies!=NULL);w.evreply=replies;w.outstanding=0;
      if(replies) {
        for(i=0;i<12;i++) {
          WQA(transfer_progress(&progress,(i+1)*32768,12*32768)==0);
          e=(NetMsg *)GetMsg(events);
          WQA(e && e->kind==NE_TRANSFER && e->a_before==(i+1)*32768);
          if(e)worker_done(e);
        }
        transfer_progress(&progress,12*32768,0);
        WQA(w.outstanding==0);DeleteMsgPort(replies);
      }
      w.evreply=events;
    }
    /* Exercise the actual bsdsocket transport against a listening local
     * socket that sends no bytes. Cancel must not wait for its 30s timeout. */
    { char error[160];int listener=-1,ready=net_amiga_open(error,sizeof(error))==0;
      WQA(ready);
      if(ready) {
        struct sockaddr_in address;socklen_t length=sizeof(address);Transport tr={0};NetOptions opt={0};
        memset(&address,0,sizeof(address));address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(0x7f000001UL);
        listener=socket(AF_INET,SOCK_STREAM,0);
        WQA(listener>=0 && bind(listener,(struct sockaddr *)&address,sizeof(address))==0 && listen(listener,1)==0 && getsockname(listener,(struct sockaddr *)&address,&length)==0);
        opt.timeout_s=30;
        if(listener>=0 && (net_amiga()->connect)(NULL,"127.0.0.1",ntohs(address.sin_port),&opt,&tr,error,sizeof(error))==0) {
            char byte;ar_ms began=network_now();WQA(tr.interrupt!=NULL);
            tr.interrupt(tr.ctx,qa_stall_cancel,&began);
            WQA(tr.read(tr.ctx,&byte,1)<0);WQA(network_now()-began<2000);
            tr.close(tr.ctx);
        } else WQA(0);
        if(listener>=0)CloseSocket(listener);
        net_amiga_close();
      }
    }
    /* Large-file streaming, truncation and cancellation exercise the actual
     * file staging/rename path without a file-sized response allocation. */
    { Worker download={0};DownloadWire wire;NetIface net={0};NetMsg command={0};BPTR f;char before[5]={0};ULONG serial=777;
      download_wire_init(&wire,5UL*1024*1024);net.ctx=&wire;net.connect=download_wire_connect;
      WQA(rc_init(&download.sess.rc,&net,"http://qa.local",1)==RC_OK);download.have_sess=download.sess.logged_in=1;download.evreply=events;
      command.kind=NC_DOWNLOAD;command.a_url="/large";command.a_text="T:AmiRocket-stream-qa.bin";
      handle(&download,&command);WQA(command.rc==RC_OK);
      f=Open(command.a_text,MODE_OLDFILE);WQA(f && Seek(f,0,OFFSET_END)==0 && Seek(f,0,OFFSET_CURRENT)==5UL*1024*1024);if(f)Close(f);
      while((e=(NetMsg *)GetMsg(events)))netmsg_free(e);
      download.outstanding=0;
      f=Open(command.a_text,MODE_NEWFILE);if(f){Write(f,"keep",4);Close(f);}
      download_wire_init(&wire,5UL*1024*1024);wire.truncate_at=4096;
      handle(&download,&command);WQA(command.rc!=RC_OK);
      f=Open(command.a_text,MODE_OLDFILE);if(f){Read(f,before,4);Close(f);}WQA(!strcmp(before,"keep"));
      while((e=(NetMsg *)GetMsg(events)))netmsg_free(e);
      download.outstanding=0;
      download_wire_init(&wire,5UL*1024*1024);wire.notify_at=4096;wire.on_read=qa_cancel_download;wire.on_read_ctx=&serial;command.sent_at=serial;transfer_cancel[0]=0;
      handle(&download,&command);WQA(command.rc!=RC_OK && strstr(command.text,"cancelled"));
      f=Open(command.a_text,MODE_OLDFILE);memset(before,0,sizeof(before));if(f){Read(f,before,4);Close(f);}WQA(!strcmp(before,"keep"));
      transfer_cancel[0]=0;while((e=(NetMsg *)GetMsg(events)))netmsg_free(e);
      DeleteFile(command.a_text);rc_cleanup(&download.sess.rc);
    }
    traffic_init(&w.traffic,123); w.interval=5; w.background_interval=30;
    w.foreground=1; WQA(poll_interval(&w)==5); w.idle_polls=6; WQA(poll_interval(&w)==30);
    w.realtime.phase=4; w.realtime.ready_mask=15; WQA(poll_interval(&w)==60);
    w.realtime.phase=0; w.realtime.ready_mask=0;
    { NetMsg options={0}; options.kind=NC_SET_INTERVAL; options.a_realtime=1; options.a_interval=5;
      handle(&w,&options); WQA(w.realtime_enabled); options.a_realtime=0;
      handle(&w,&options); WQA(!w.realtime_enabled && !w.realtime.phase);
    }
    w.idle_polls=0; w.foreground=0; WQA(poll_interval(&w)==30);
    { ar_ms now=network_now(); unsigned long before=w.traffic.requests;
      network_after(&w,429,120); e=(NetMsg *)GetMsg(events);
      WQA(w.traffic.cooldown>=now+120000 && w.net_trouble && e && strstr(e->text,"paused"));
      if(e) netmsg_free(e);
      WQA(network_before(&w)<0 && w.traffic.requests==before);
      w.traffic.cooldown=0;
    }
    WQA(command_priority(NC_SEND)<command_priority(NC_AVATAR));
    WQA(command_priority(NC_OPEN_ROOM)<command_priority(NC_PRESENCE));
    { NetMsg a={0},b={0}; a.kind=b.kind=NC_POLL_NOW; a.server_slot=b.server_slot=1;
      WQA(duplicate_command(&a,&b)); b.server_slot=2; WQA(!duplicate_command(&a,&b));
      b.server_slot=1; a.kind=b.kind=NC_SEND; WQA(!duplicate_command(&a,&b));
    }
    { NetMsg close={0},pending={0}; close.kind=NC_DISCONNECT; close.server_slot=pending.server_slot=1;
      close.server_generation=pending.server_generation=9; pending.kind=NC_CONNECT;
      WQA(cancel_after_disconnect(&pending,&close,1));
      WQA(!cancel_after_disconnect(&pending,&close,0));
      pending.server_generation=8; WQA(cancel_after_disconnect(&pending,&close,0));
      pending.server_generation=9; pending.kind=NC_SEND; WQA(cancel_after_disconnect(&pending,&close,0));
      pending.kind=NC_SET_INTERVAL; WQA(!cancel_after_disconnect(&pending,&close,1));
      pending.kind=NC_SEND; pending.server_slot=2; WQA(!cancel_after_disconnect(&pending,&close,1));
      close.kind=NC_LOGOUT; pending.server_slot=1; pending.kind=NC_CONNECT;
      WQA(!cancel_after_disconnect(&pending,&close,0));
    }
    { NetMsg disconnect={0}; ar_ms deadline=network_now()+120000;
      disconnect.kind=NC_DISCONNECT; disconnect.server_generation=w.generation;
      w.traffic.cooldown=deadline; handle(&w,&disconnect);
      WQA(w.traffic.cooldown==deadline); w.traffic.cooldown=0;
    }
    { Buf data,loaded; BPTR dir; char path[96]; int slot;
      buf_init(&data); buf_init(&loaded); buf_puts(&data,"avatar-fixture");
      w.cache_enabled=1; strcpy(w.cache_origin,"https://cache-test.invalid/chat"); strcpy(w.sess.rc.user_id,"account-1");
      WQA(avatar_cache_at(&w,"user",&data,1,"RAM:AmiRocket-avatar-qa"));
      WQA(avatar_cache_at(&w,"user",&loaded,0,"RAM:AmiRocket-avatar-qa") && loaded.len==data.len && !memcmp(loaded.data,data.data,data.len));
      { BPTR f=0; ar_ms expired=network_now()-86400001; Buf oversized={0};
        for(slot=0;slot<64 && !f;slot++) { snprintf(path,sizeof(path),"RAM:AmiRocket-avatar-qa/%02d.dat",slot); f=Open(path,MODE_OLDFILE); }
        WQA(f!=0);
        if(f) { Seek(f,768,OFFSET_BEGINNING); WQA(Write(f,&expired,sizeof(expired))==sizeof(expired)); Close(f); }
        WQA(!avatar_cache_at(&w,"user",&loaded,0,"RAM:AmiRocket-avatar-qa"));
        WQA(avatar_cache_at(&w,"user",&data,1,"RAM:AmiRocket-avatar-qa"));
        oversized.len=128UL*1024+1;
        WQA(!avatar_cache_at(&w,"user",&oversized,1,"RAM:AmiRocket-avatar-qa"));
      }
      strcpy(w.sess.rc.user_id,"account-2"); WQA(!avatar_cache_at(&w,"user",&loaded,0,"RAM:AmiRocket-avatar-qa"));
      w.cache_enabled=0; WQA(!avatar_cache_at(&w,"user",&loaded,0,"RAM:AmiRocket-avatar-qa"));
      for(slot=0;slot<64;slot++) { snprintf(path,sizeof(path),"RAM:AmiRocket-avatar-qa/%02d.dat",slot); DeleteFile(path); }
      DeleteFile("RAM:AmiRocket-avatar-qa"); (void)dir;
      buf_free(&data); buf_free(&loaded);
    }
    { NetMsg cancelled={0}; cancelled.kind=NC_UPLOAD; cancelled.server_generation=w.generation; cancelled.sent_at=999;
      transfer_cancel[w.slot]=999; w.traffic.cooldown=network_now()+120000;
      handle(&w,&cancelled); WQA(cancelled.rc==RC_ERR_ARGS && strstr(cancelled.text,"cancelled"));
      transfer_cancel[w.slot]=0;
    }
    /* Real separate-process worker: repeated command replies must settle back
     * to approximately one wake per second while disconnected. A signal loop
     * produces hundreds/thousands here even though it does no HTTP work. */
    { char error[160]; int started; ULONG wakes;
      qa_worker_wakes=0; started=worker_start(events,error,sizeof(error)); WQA(started==0);
      if(!started) {
        for(i=0;i<8;i++) { NetMsg *command=netmsg_new(NC_SET_FOCUS);
          if(command) { command->server_slot=0; worker_send(command); }
          Delay(2);
          while((e=(NetMsg *)GetMsg(events))) netmsg_free(e);
        }
        Delay(150); wakes=qa_worker_wakes;
        WQA(wakes>=3 && wakes<=20);
        worker_stop(); while((e=(NetMsg *)GetMsg(events))) netmsg_free(e);
        printf("QA worker idle: %lu wakes during 3s plus 8 command wakes\n",(unsigned long)wakes);
      }
    }
    g_guiport=saved; DeleteMsgPort(events); return fail;
#undef WQA
}

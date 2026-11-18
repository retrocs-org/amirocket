/* Test the actual worker state machine without a live account or network. */
#include "../src/amiga/worker.c"
int worker_recovery_qa(int *passed)
{
    Worker w={0}; struct MsgPort *saved=g_guiport,*events=CreateMsgPort(); NetMsg *e; int fail=0,i;
#define WQA(x) do { if(x) (*passed)++; else fail++; } while(0)
    if(!events) return 1;
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
    g_guiport=saved; DeleteMsgPort(events); return fail;
#undef WQA
}

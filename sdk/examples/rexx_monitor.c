/* Dock integration example: event-driven snapshots plus a ten-second fallback.
 * Stack 65536; AmiRocketMonitor [AMIROCKET or AMIROCKET.2]. Ctrl-C exits.
 * No chat text is executed and no chat messages are sent. */
#include <exec/types.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/rexxsyslib.h>
#include "../include/amirocket_rexx.h"
struct RxsLib *RexxSysBase;
static char monitor_name[64],client[64],last_epoch[80];
static struct MsgPort *port;
static struct RexxMsg *requests[2],*retired[8];
static unsigned long poll_count,request_poll[2];
static int available=-1;
static unsigned long last_sequence;
#ifdef AR_MONITOR_QA
#include "../../tools/qa_observation.h"
/* Test-only observation files are closed immediately: the harness never reads
 * a log stream still owned by the child process. All protocol code is shared. */
static void qa_report(const char *path,const char *text)
{
    BPTR f; if(!qa_observation_take()) return;
    f=Open(path,MODE_NEWFILE); if(f) { Write(f,(APTR)text,strlen(text)); Close(f); }
    qa_observation_release();
}
#else
#define qa_report(path,text) ((void)0)
#endif


static void free_request(struct RexxMsg *m)
{
    if(!m->rm_Result1 && m->rm_Result2) DeleteArgstring((char *)m->rm_Result2);
    if(m->rm_Args[0]) DeleteArgstring(m->rm_Args[0]);
    if(m->rm_CommAddr) DeleteArgstring(m->rm_CommAddr);
    if(m->rm_FileExt) DeleteArgstring(m->rm_FileExt);
    DeleteRexxMsg(m);
}
static int send_command(int index,const char *text)
{
    struct RexxMsg *m; struct MsgPort *target;
    if(requests[index]) return 0;
    m=CreateRexxMsg(port,"rexx",monitor_name); if(!m) return 0;
    m->rm_Action=RXCOMM|RXFF_RESULT;
    m->rm_Args[0]=CreateArgstring(text,strlen(text));
    m->rm_CommAddr=CreateArgstring(monitor_name,strlen(monitor_name)); m->rm_FileExt=CreateArgstring("rexx",4);
    if(!m->rm_Args[0] || !m->rm_CommAddr || !m->rm_FileExt) { free_request(m); return 0; }
    Forbid(); target=FindPort(client);
    if(target) { requests[index]=m; request_poll[index]=poll_count; PutMsg(target,(struct Message *)m); }
    Permit();
    if(!target) { free_request(m); return 0; }
    return 1;
}
static void subscribe(void)
{
    char command[100]; snprintf(command,sizeof(command),"SUBSCRIBE %s 2",monitor_name); send_command(0,command);
}
static void receive(int *dirty)
{
    struct RexxMsg *m; int i;
    while((m=(struct RexxMsg *)GetMsg(port))) {
        for(i=0;i<2 && requests[i]!=m;i++);
        if(i<2) {
            if(i==1 && !m->rm_Result1 && m->rm_Result2) {
                const char *snapshot=(char *)m->rm_Result2; unsigned long unread,connected,iconified;
                if(ar_rexx_field(snapshot,"epoch",last_epoch,sizeof(last_epoch)) && ar_rexx_number(snapshot,"seq",&last_sequence) &&
                   ar_rexx_number(snapshot,"unread",&unread) && ar_rexx_number(snapshot,"connected",&connected) && ar_rexx_number(snapshot,"iconified",&iconified)) {
                    printf("Dock state: unread=%lu connected=%lu iconified=%lu available=1\n%s\n",unread,connected,iconified,snapshot);
                    available=1; qa_report("T:ARDockState",snapshot); fflush(stdout); Flush(Output());
                } else printf("Unsupported snapshot; check CAPABILITIES.\n");
            }
            requests[i]=NULL; free_request(m); continue;
        }
        for(i=0;i<8 && retired[i]!=m;i++);
        if(i<8) { retired[i]=NULL; free_request(m); continue; }
        if(m->rm_Node.mn_Node.ln_Type==NT_REPLYMSG) continue;
        if(m->rm_Node.mn_Length==sizeof(*m) && (m->rm_Action&RXCODEMASK)==RXCOMM && m->rm_Args[0]) {
            ArDockEvent event;
            if(ar_rexx_event2(m->rm_Args[0],&event)) {
                if(strcmp(event.epoch,last_epoch) || event.sequence!=last_sequence+1 || event.truncated || !strcmp(event.kind,"RESYNC"))
                    printf("Refreshing dock state after restart, gap or resync.\n");
                snprintf(last_epoch,sizeof(last_epoch),"%s",event.epoch); last_sequence=event.sequence;
                *dirty=1; /* Coalesced refresh for badges and connection state. */
            }
        }
        m->rm_Result1=m->rm_Result2=0; ReplyMsg((struct Message *)m);
    }
}
/* Retain ownership of timed-out packets until their recipients reply.
 * Eight retired packets bound failures without blocking the dock's timer. */
static void check_client(int *dirty)
{
    struct MsgPort *target; int i,j;
    Forbid(); target=FindPort(client); Permit();
    if(!target && available!=0) {
        qa_report("T:ARDockState","unread=0 connected=0 iconified=0 available=0");
        printf("Dock state: unread=0 connected=0 iconified=0 available=0\nAmiRocket offline.\n"); fflush(stdout); Flush(Output());
        last_epoch[0]=0; last_sequence=0; available=0;
    }
    for(i=0;i<2;i++) if(requests[i] && (!target || poll_count-request_poll[i]>=2)) {
        for(j=0;j<8 && retired[j];j++);
        if(j<8) { retired[j]=requests[i]; requests[i]=NULL; }
        if(target && available!=0) {
            qa_report("T:ARDockState","unread=0 connected=0 iconified=0 available=0");
            printf("Dock state: unread=0 connected=0 iconified=0 available=0\nAmiRocket is unresponsive; refresh when replies resume.\n");
            available=0; fflush(stdout); Flush(Output());
        }
        *dirty=1;
    }
}
int main(int argc,char **argv)
{
    struct MsgPort *timer_port; struct timerequest *timer; int open=0,pending=0,stop=0,dirty=1,published=0,result=20;
    if(argc>2 || (argc==2 && strlen(argv[1])>=sizeof(client))) return 10;
    snprintf(client,sizeof(client),"%s",argc==2?argv[1]:"AMIROCKET");
    RexxSysBase=(struct RxsLib *)OpenLibrary("rexxsyslib.library",36); if(!RexxSysBase) return 20;
    port=CreateMsgPort(); timer_port=CreateMsgPort();
    timer=timer_port?(struct timerequest *)CreateIORequest(timer_port,sizeof(*timer)):NULL;
    if(!port || !timer || OpenDevice(TIMERNAME,UNIT_VBLANK,(struct IORequest *)timer,0)) stop=1;
    else open=1;
    snprintf(monitor_name,sizeof(monitor_name),"AMIROCKET.MONITOR.%lx",(unsigned long)FindTask(NULL));
    if(!stop) {
        Forbid();
        if(FindPort(monitor_name)) stop=1;
        else { port->mp_Node.ln_Name=monitor_name; AddPort(port); published=1; result=0; }
        Permit();
    }
    if(!stop) { qa_report("T:ARDockPort",monitor_name); printf("Monitor port: %s\n",monitor_name); fflush(stdout); Flush(Output()); check_client(&dirty); subscribe(); }
    while(!stop) {
        ULONG signals;
        receive(&dirty);
        if(dirty && !requests[1] && send_command(1,"DOCKSTATUS")) dirty=0;
        if(!pending) {
            timer->tr_node.io_Command=TR_ADDREQUEST; timer->tr_time.tv_secs=AR_REXX_POLL_SECONDS; timer->tr_time.tv_micro=0;
            SendIO((struct IORequest *)timer); pending=1;
        }
        signals=Wait((1UL<<port->mp_SigBit)|(1UL<<timer_port->mp_SigBit)|SIGBREAKF_CTRL_C);
        if(signals&SIGBREAKF_CTRL_C) stop=1;
        if((signals&(1UL<<timer_port->mp_SigBit)) && CheckIO((struct IORequest *)timer)) {
            WaitIO((struct IORequest *)timer); pending=0; poll_count++; dirty=1; check_client(&dirty); subscribe();
        }
    }
    if(pending) { if(!CheckIO((struct IORequest *)timer)) AbortIO((struct IORequest *)timer); WaitIO((struct IORequest *)timer); }
    if(open) CloseDevice((struct IORequest *)timer);
    if(timer) DeleteIORequest((struct IORequest *)timer);
    if(timer_port) DeleteMsgPort(timer_port);
    if(port) {
        if(published) RemPort(port);
        receive(&dirty);
        { int i,busy=requests[0]!=NULL || requests[1]!=NULL;
          for(i=0;i<8;i++) if(retired[i]) busy=1;
        if(busy) {
            /* Never free packets still owned by a missing/hung client. Bounded
             * public storage permits late replies without signalling a dead task. */
            port->mp_Flags=PA_IGNORE;
        } else { DeleteMsgPort(port); CloseLibrary((struct Library *)RexxSysBase); }
        }
    } else CloseLibrary((struct Library *)RexxSysBase);
    qa_report("T:ARDockStopped","done"); printf("Monitor stopped.\n"); fflush(stdout); Flush(Output());
    return result;
}

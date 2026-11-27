#include "qa_observation.h"
/* Real separately launched monitor/client processes, only in disposable profiles. */
static char dock_log[16384];
static const char *qa_dock_read(const char *path)
{
    BPTR file; LONG n;
    if(!qa_observation_take()) return NULL;
    file=Open(path,MODE_OLDFILE);
    dock_log[0]=0; if(!file) { qa_observation_release(); return NULL; }
    n=Read(file,dock_log,sizeof(dock_log)-1); Close(file); qa_observation_release(); if(n<0) return NULL; dock_log[n]=0;
    return dock_log;
}
static void qa_dock_pump(void)
{
    /* The fixture has stopped its network worker. Do not pump its window:
     * activation would reload saved auto-connect profiles into the fixture.
     * The separately launched production client handles its own GUI normally. */
    arexx_handle(); Delay(1);
}
static int qa_dock_state(const char *state,int ticks)
{
    int i; for(i=0;i<ticks;i++) {
        const char *last=qa_dock_read("T:ARDockState"); unsigned long unread,connected,iconified,available=1; char fields[160];
        if(last && ar_rexx_number(last,"unread",&unread) && ar_rexx_number(last,"connected",&connected) && ar_rexx_number(last,"iconified",&iconified)) {
            ar_rexx_number(last,"available",&available);
            snprintf(fields,sizeof(fields),"unread=%lu connected=%lu iconified=%lu available=%lu",unread,connected,iconified,available);
            if(!strcmp(fields,state)) return 1;
        }
        qa_dock_pump();
    }
    printf("Dock state timeout: expected %s; last snapshot: %s\n",state,qa_dock_read("T:ARDockState"));
    return 0;
}
static int qa_dock_port(const char *name,int present,int ticks)
{
    int i,found; for(i=0;i<ticks;i++) {
        Forbid(); found=FindPort(name)!=NULL; Permit();
        if(found==present) return 1;
        qa_dock_pump();
    }
    return 0;
}
static int qa_dock_monitor_start(const char *client)
{
    char command[200]; BPTR script;
    DeleteFile("T:ARDockState"); DeleteFile("T:ARDockPort"); DeleteFile("T:ARDockStopped");
    script=Open("T:ARDockMonitorStart",MODE_NEWFILE); if(!script) return 0;
    snprintf(command,sizeof(command),"Stack 131072\nAmiRocket:AmiRocketMonitorQA %s\n",client);
    Write(script,command,strlen(command)); Close(script);
    return launch_helper("Execute T:ARDockMonitorStart")>=0;
}
static int qa_dock_monitor_stop(void)
{
    const char *p=qa_dock_read("T:ARDockPort"); char name[64]; int n=0,i; struct MsgPort *port;
    if(!p) return 0;
    while(*p && *p!='\n' && *p!='\r' && n<63) name[n++]=*p++;
    name[n]=0;
    Forbid(); port=FindPort(name); if(port) Signal(port->mp_SigTask,SIGBREAKF_CTRL_C); Permit();
    for(i=0;i<250;i++) { if(qa_dock_read("T:ARDockStopped")) return qa_dock_port(name,0,100); qa_dock_pump(); }
    return 0;
}
/* Sender is the harness process; receiver is the real client process. */
static int qa_dock_command(const char *client,const char *command,char *out,int size)
{
    struct MsgPort *reply=CreateMsgPort(),*target; struct RexxMsg *m,*got=NULL; int i,rc=20;
    if(!reply) return rc;
    m=CreateRexxMsg(reply,"rexx","AR.QA.DOCK");
    if(!m) { DeleteMsgPort(reply); return rc; }
    m->rm_Action=RXCOMM|RXFF_RESULT; m->rm_Args[0]=CreateArgstring(command,strlen(command));
    if(m->rm_Args[0]) {
        Forbid(); target=FindPort(client); if(target) PutMsg(target,(struct Message *)m); Permit();
        if(target) {
            for(i=0;i<500 && !got;i++) { qa_dock_pump(); got=(struct RexxMsg *)GetMsg(reply); }
            if(!got) { reply->mp_Flags=PA_IGNORE; return 20; } /* Recipient still owns packet. */
            rc=got->rm_Result1;
            if(!rc && got->rm_Result2) ar_strlcpy(out,(char *)got->rm_Result2,size);
        } else rc=10;
    }
    if(!m->rm_Result1 && m->rm_Result2) DeleteArgstring((char *)m->rm_Result2);
    if(m->rm_Args[0]) DeleteArgstring(m->rm_Args[0]);
    DeleteRexxMsg(m); DeleteMsgPort(reply); return rc;
}
static void qa_dock_processes(void)
{
    Prefs *saved=ar_malloc(sizeof(Prefs)),*offline=ar_malloc(sizeof(Prefs));
    int unread=G.rooms.items[0].unread,connected=G.connected,started=0,client_live=0; char name[32],reply[4096],epoch[80],next_epoch[80];
    qa_trace("QA separate dock monitor: aggregate badges and notifications\n");
    QA(saved && offline); if(!saved || !offline) goto done;
    *saved=G.prefs; prefs_defaults(offline);
    strcpy(offline->server,"https://dock-one.test"); prefs_migrate_servers(offline);
    offline->servers[0].autoconnect=0; offline->servers[1]=offline->servers[0];
    strcpy(offline->servers[1].url,"https://dock-two.test");
    qa_trace("QA dock prepare profiles\n");
    G.prefs=*offline; servers_init(); G.connected=1; G.server_view=1;
    G.rooms.items[0].unread=7; rc_roomlist_copy(&G.servers[1].rooms,&G.rooms);
    G.servers[1].rooms.items[0].unread=5; G.servers[1].connected=1;
    qa_trace("QA dock start host\n");
    QA(arexx_start(control_command)); ar_strlcpy(name,arexx_port_name(),sizeof(name));
    qa_trace("QA dock launch monitor\n");
    started=qa_dock_monitor_start(name); QA(started);
    if(started) {
        qa_trace("QA dock wait initial badge\n");
        QA(qa_dock_state("unread=12 connected=1 iconified=0 available=1",500));
        qa_trace("QA dock wait badge event\n");
        G.servers[1].rooms.items[0].unread=9; notify_unread();
        QA(qa_dock_state("unread=16 connected=1 iconified=0 available=1",250));
        /* Poll fallback must reconcile even if no notification was emitted. */
        qa_trace("QA dock wait polling badge\n");
        G.servers[1].rooms.items[0].unread=3;
        QA(qa_dock_state("unread=10 connected=1 iconified=0 available=1",650));
        qa_trace("QA dock stop monitor\n");
        QA(qa_dock_monitor_stop()); started=0;
    }
    arexx_stop(); servers_cleanup(); G.multi=0; G.connected=connected; G.rooms.items[0].unread=unread;
    G.prefs=*saved; G.server_view=0; G.active_server=0;
    qa_trace("QA real client and dock restarts, SHOW and ICONIFY over ARexx\n");
    QA(prefs_save(offline)==0);
    /* No auto-connect and no credentials: real process stays offline. */
    DeleteFile("T:ARDockClientDone");
    { BPTR script=Open("T:ARDockClient",MODE_NEWFILE); const char *text="Stack 131072\nAmiRocket:AmiRocket >T:ARDockClient.log\nEcho >T:ARDockClientDone done\n";
      QA(script!=0); if(script) { Write(script,(APTR)text,strlen(text)); Close(script); }
    }
    RexxSysBase=(struct RxsLib *)OpenLibrary("rexxsyslib.library",36); QA(RexxSysBase!=NULL);
    if(!RexxSysBase) goto restore;
    QA(launch_helper("Execute T:ARDockClient")>=0); client_live=qa_dock_port("AMIROCKET",1,500); QA(client_live);
    if(!client_live) goto restore;
    QA(qa_dock_command("AMIROCKET","DOCKSTATUS",reply,sizeof(reply))==0 && ar_rexx_field(reply,"epoch",epoch,sizeof(epoch)));
    started=qa_dock_monitor_start("AMIROCKET"); QA(started);
    if(started) QA(qa_dock_state("unread=0 connected=0 iconified=0 available=1",500));
    QA(qa_dock_command("AMIROCKET","ICONIFY",reply,sizeof(reply))==0);
    if(started) QA(qa_dock_state("unread=0 connected=0 iconified=1 available=1",250));
    QA(qa_dock_command("AMIROCKET","SHOW",reply,sizeof(reply))==0);
    if(started) QA(qa_dock_state("unread=0 connected=0 iconified=0 available=1",250));
    QA(qa_dock_command("AMIROCKET","QUIT",reply,sizeof(reply))==0);
    QA(qa_dock_port("AMIROCKET",0,500)); client_live=0;
    if(started) QA(qa_dock_state("unread=0 connected=0 iconified=0 available=0",650));
    DeleteFile("T:ARDockClientDone");
    QA(launch_helper("Execute T:ARDockClient")>=0); client_live=qa_dock_port("AMIROCKET",1,500); QA(client_live);
    QA(qa_dock_command("AMIROCKET","DOCKSTATUS",reply,sizeof(reply))==0 && ar_rexx_field(reply,"epoch",next_epoch,sizeof(next_epoch)) && strcmp(epoch,next_epoch));
    if(started) { QA(qa_dock_state("unread=0 connected=0 iconified=0 available=1",650)); QA(qa_dock_monitor_stop()); started=0; }
    /* Restart the dock without restarting the client: subscription and snapshot rebuild. */
    started=qa_dock_monitor_start("AMIROCKET"); QA(started);
    if(started) {
        QA(qa_dock_state("unread=0 connected=0 iconified=0 available=1",500));
        QA(qa_dock_command("AMIROCKET","ICONIFY",reply,sizeof(reply))==0);
        QA(qa_dock_state("unread=0 connected=0 iconified=1 available=1",250));
        QA(qa_dock_command("AMIROCKET","SHOW",reply,sizeof(reply))==0);
        QA(qa_dock_state("unread=0 connected=0 iconified=0 available=1",250));
        qa_trace("QA dock stop monitor\n");
        QA(qa_dock_monitor_stop()); started=0;
    }
restore:
    if(started) qa_dock_monitor_stop();
    if(client_live) { qa_dock_command("AMIROCKET","QUIT",reply,sizeof(reply)); qa_dock_port("AMIROCKET",0,500); }
    /* Wait for DOS to finish process cleanup/autosaves before restoring settings. */
    { int i; BPTR marker=0; for(i=0;i<250 && !marker;i++) { marker=Open("T:ARDockClientDone",MODE_OLDFILE); if(!marker) qa_dock_pump(); } QA(marker!=0); if(marker) Close(marker); }
    if(RexxSysBase) { CloseLibrary((struct Library *)RexxSysBase); RexxSysBase=NULL; }
    prefs_save(saved); DeleteFile("T:ARDockClient"); DeleteFile("T:ARDockClientDone");
done:
    if(saved) ar_free(saved);
    if(offline) ar_free(offline);
}

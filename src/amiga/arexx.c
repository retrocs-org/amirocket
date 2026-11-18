#include "arexx.h"
#include <exec/ports.h>
#include <exec/memory.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/rexxsyslib.h>
#include <string.h>
#include <stdio.h>
struct RxsLib *RexxSysBase;
static struct MsgPort *port;
static ArRexxCommand dispatch;
static char name[32], subscribers[8][64];
static struct RexxMsg *outgoing[16];
static int pending,versions[8],inflight[8],needs_resync[8];
static unsigned long generations[8],outgoing_generation[16],sequence,dropped;
static int outgoing_slot[16];
static char epoch[80];
static void flush_resync(void);
const char *arexx_epoch(void) { return epoch[0]?epoch:"none"; }
ULONG arexx_sequence(void) { return sequence; }
ULONG arexx_dropped(void) { return dropped; }
int arexx_pending(void) { return pending; }
int arexx_is_packet(const struct RexxMsg *m)
{
    /* IsRexxMsg identifies interpreter-origin packets. Native application
     * packets made by CreateRexxMsg have no interpreter task block. */
    return m && (IsRexxMsg(m) || (m->rm_Node.mn_Length==sizeof(*m) &&
        (m->rm_Action & RXCODEMASK)==RXCOMM && m->rm_Args[0]));
}
static void release_message(struct RexxMsg *m)
{
    int i;
    for(i=0;i<16;i++) if(outgoing[i]==m) {
        int slot=outgoing_slot[i];
        if(slot>=0 && slot<8 && outgoing_generation[i]==generations[slot] && inflight[slot]>0) inflight[slot]--;
        outgoing[i]=NULL; pending--; break;
    }
    if(i==16) return;
    if(m->rm_Args[0]) DeleteArgstring(m->rm_Args[0]);
    if(m->rm_CommAddr) DeleteArgstring(m->rm_CommAddr);
    if(m->rm_FileExt) DeleteArgstring(m->rm_FileExt);
    DeleteRexxMsg(m);
}
int arexx_start(ArRexxCommand command)
{
    int i; struct DateStamp stamp; ULONG identity;
    struct Library *UtilityBase;
    if(port) return 1;
    UtilityBase=OpenLibrary("utility.library",39); if(!UtilityBase) return 0;
    identity=GetUniqueID(); CloseLibrary(UtilityBase);
    DateStamp(&stamp);
    snprintf(epoch,sizeof(epoch),"%lx-%lx-%lx-%lx",(unsigned long)identity,(unsigned long)stamp.ds_Days,(unsigned long)stamp.ds_Minute,(unsigned long)stamp.ds_Tick);
    sequence=dropped=0; pending=0; memset(outgoing,0,sizeof(outgoing));
    memset(subscribers,0,sizeof(subscribers)); memset(inflight,0,sizeof(inflight)); memset(needs_resync,0,sizeof(needs_resync));
    RexxSysBase=(struct RxsLib *)OpenLibrary("rexxsyslib.library",36); if(!RexxSysBase) return 0;
    port=CreateMsgPort(); if(!port) { CloseLibrary((struct Library *)RexxSysBase); RexxSysBase=NULL; return 0; }
    dispatch=command; Forbid();
    for(i=1;i<100;i++) {
        if(i==1) strcpy(name,"AMIROCKET"); else snprintf(name,sizeof(name),"AMIROCKET.%d",i);
        if(!FindPort(name)) break;
    }
    if(i==100) { Permit(); DeleteMsgPort(port); port=NULL; CloseLibrary((struct Library *)RexxSysBase); RexxSysBase=NULL; return 0; }
    port->mp_Node.ln_Name=name; AddPort(port); Permit(); return 1;
}
ULONG arexx_signal(void) { return port ? 1UL<<port->mp_SigBit : 0; }
const char *arexx_port_name(void) { return port ? name : ""; }
void arexx_handle(void)
{
    struct RexxMsg *m;
    if(!port) return;
    while((m=(struct RexxMsg *)GetMsg(port))) {
        if(m->rm_Node.mn_Node.ln_Type==NT_REPLYMSG) { release_message(m); continue; }
        if(!arexx_is_packet(m)) { ReplyMsg((struct Message *)m); continue; }
        { char result[4096]; int rc;
          result[0]=0;
          rc=dispatch && m->rm_Args[0] ? dispatch(m->rm_Args[0],result,sizeof(result)) : 10;
          m->rm_Result1=rc; m->rm_Result2=0;
          if(!rc && (m->rm_Action & RXFF_RESULT)) {
              m->rm_Result2=(LONG)CreateArgstring(result,strlen(result));
              if(!m->rm_Result2) m->rm_Result1=20;
          }
          ReplyMsg((struct Message *)m);
        }
    }
    flush_resync();
}
int arexx_subscribe(const char *target) { return arexx_subscribe_version(target,1); }
int arexx_subscribe_version(const char *target,int version)
{
    int i,empty=-1,exists;
    if((version!=1 && version!=2) || !target || !*target || strlen(target)>=64 || !strcmp(target,name)) return 0;
    Forbid(); exists=FindPort(target)!=NULL; Permit(); if(!exists) return 0;
    for(i=0;i<8;i++) {
        if(subscribers[i][0]) { int alive; Forbid(); alive=FindPort(subscribers[i])!=NULL; Permit();
            if(!alive) { subscribers[i][0]=0; generations[i]++; inflight[i]=needs_resync[i]=0; } }
        if(!strcmp(subscribers[i],target)) { if(versions[i]!=version) needs_resync[i]=version==2; versions[i]=version; return 1; } if(!subscribers[i][0]) empty=i; }
    if(empty<0) return 0;
    strcpy(subscribers[empty],target); versions[empty]=version; generations[empty]++;
    inflight[empty]=0; needs_resync[empty]=version==2; return 1;
}
int arexx_unsubscribe(const char *target)
{
    int i; for(i=0;i<8;i++) if(!strcmp(subscribers[i],target)) { subscribers[i][0]=0; generations[i]++; inflight[i]=needs_resync[i]=0; return 1; } return 0;
}
/* Independent field budgets guarantee valid quoting even for oversized data. */
static void quoted(char *out,int *used,const char *s,int limit,int *truncated)
{
    int n=0; out[(*used)++]='"';
    while(s && *s && n<limit) {
        unsigned char c=*s++; n++;
        if(c=='"') out[(*used)++]='"';
        out[(*used)++]=c<32?' ':c;
    }
    if(s && *s) *truncated=1;
    out[(*used)++]='"'; out[*used]=0;
}
static int send_event(int slot,const char *kind,const char *room,const char *user,const char *text)
{
    char command[1536]; int used,j,truncated=0; struct RexxMsg *m; struct MsgPort *target;
    if(pending>=16 || inflight[slot]>=2) return 0;
    if(versions[slot]==2) used=snprintf(command,sizeof(command),"AMIROCKET_EVENT2 %s %lu %.31s ",epoch,sequence,kind);
    else used=snprintf(command,sizeof(command),"AMIROCKET_EVENT %.31s ",kind);
    quoted(command,&used,room,128,&truncated); command[used++]=' ';
    quoted(command,&used,user,96,&truncated); command[used++]=' ';
    quoted(command,&used,text,384,&truncated);
    if(versions[slot]==2) snprintf(command+used,sizeof(command)-used," dropped=%lu truncated=%d",dropped,truncated);
    for(j=0;j<16 && outgoing[j];j++);
    if(j==16) return 0;
    m=CreateRexxMsg(port,"rexx",name); if(!m) return 0;
    m->rm_Args[0]=CreateArgstring(command,strlen(command)); m->rm_Action=RXCOMM;
    m->rm_CommAddr=CreateArgstring(name,strlen(name)); m->rm_FileExt=CreateArgstring("rexx",4);
    if(!m->rm_Args[0] || !m->rm_CommAddr || !m->rm_FileExt) {
        if(m->rm_Args[0]) DeleteArgstring(m->rm_Args[0]);
        if(m->rm_CommAddr) DeleteArgstring(m->rm_CommAddr);
        if(m->rm_FileExt) DeleteArgstring(m->rm_FileExt);
        DeleteRexxMsg(m); return 0;
    }
    Forbid(); target=FindPort(subscribers[slot]);
    if(target) {
        outgoing[j]=m; outgoing_slot[j]=slot; outgoing_generation[j]=generations[slot];
        pending++; inflight[slot]++; PutMsg(target,(struct Message *)m);
    }
    Permit();
    if(!target) {
        DeleteArgstring(m->rm_Args[0]); DeleteArgstring(m->rm_CommAddr); DeleteArgstring(m->rm_FileExt); DeleteRexxMsg(m);
        subscribers[slot][0]=0; generations[slot]++; inflight[slot]=needs_resync[slot]=0;
    }
    return target!=NULL;
}
static void flush_resync(void)
{
    int i;
    if(!port) return;
    for(i=0;i<8;i++) if(subscribers[i][0] && needs_resync[i] && inflight[i]<2) {
        if(send_event(i,"RESYNC","","","Query DOCKSTATUS; refresh ROOMS if needed")) needs_resync[i]=0;
    }
}
void arexx_event(const char *kind,const char *room,const char *user,const char *text)
{
    int i;
    if(!port) return;
    sequence++; /* Advances even when a receiver cannot accept this event. */
    for(i=0;i<8;i++) if(subscribers[i][0]) {
        if(!send_event(i,kind,room?room:"",user?user:"",text?text:"")) {
            dropped++; if(subscribers[i][0]) needs_resync[i]=1;
        }
    }
}
void arexx_stop(void)
{
    int ticks;
    if(!port) return;
    RemPort(port);
    for(ticks=0;pending && ticks<50;ticks++) { arexx_handle(); if(pending) Delay(1); }
    arexx_handle();
    if(pending) {
        /* A broken subscriber still owns these messages. Keep bounded public
         * storage valid; late replies must not signal a terminated task. */
        port->mp_Flags=PA_IGNORE; port=NULL; RexxSysBase=NULL;
    } else {
        DeleteMsgPort(port); port=NULL;
        CloseLibrary((struct Library *)RexxSysBase); RexxSysBase=NULL;
    }
    memset(subscribers,0,sizeof(subscribers));
}

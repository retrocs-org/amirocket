#include "worker.h"
#include "prefs.h"
#include "net_amiga.h"
#include "../core/mem.h"
#include "../core/session.h"
#include "../core/util.h"
#include "../core/traffic.h"
#include "../core/charset.h"
#include "../core/realtime.h"

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>
#include <stdio.h>

#define WORKER_STACK 65536

static struct Task    *g_parent;
static BYTE            g_startsig = -1;
static volatile int    g_started;
static struct MsgPort *g_cmdport;   /* owned by the worker */
static struct MsgPort *g_guiport;   /* owned by the GUI */
static struct Task    *g_worker;
static char            g_starterr[128];

/* ---- messages ------------------------------------------------------ */

NetMsg *netmsg_new(int kind)
{
    NetMsg *m = (NetMsg *)ar_calloc(1, sizeof(NetMsg));
    if (!m)
        return NULL;
    m->msg.mn_Node.ln_Type = NT_MESSAGE;
    m->msg.mn_Length = sizeof(NetMsg);
    m->kind = kind;
    rc_roomlist_init(&m->rooms);
    rc_msglist_init(&m->msgs);
    buf_init(&m->avatar);
    return m;
}

void netmsg_free_payload(NetMsg *m)
{
    rc_roomlist_free(&m->rooms);
    rc_msglist_free(&m->msgs);
    buf_free(&m->avatar);
}

static void wipe_free(char *s)
{
    if (s) {
        memset(s, 0, strlen(s));
        ar_free(s);
    }
}

void netmsg_free(NetMsg *m)
{
    if (!m)
        return;
    netmsg_free_payload(m);
    ar_free(m->a_url);
    ar_free(m->a_user);
    wipe_free(m->a_pass);
    ar_free(m->a_code);
    ar_free(m->a_uid);
    wipe_free(m->a_token);
    ar_free(m->a_rid);
    ar_free(m->a_text);
    ar_free(m->members);
    memset(m->token, 0, sizeof(m->token));
    ar_free(m);
}

void worker_send(NetMsg *m)
{
    m->msg.mn_ReplyPort = g_guiport;
    PutMsg(g_cmdport, &m->msg);
}

void worker_done(NetMsg *m)
{
    netmsg_free_payload(m);
    ReplyMsg(&m->msg);
}

/* ---- worker side --------------------------------------------------- */

/* Shared scalar cancellation IDs; no message queue wait behind the transfer. */
static volatile ULONG transfer_cancel[PREFS_SERVERS_MAX];
void worker_cancel_transfer(int slot,ar_ms serial)
{
    if(slot>=0 && slot<PREFS_SERVERS_MAX) transfer_cancel[slot]=(ULONG)serial;
}
typedef struct Worker {
    struct Worker *all_workers;
    int slot, generation,foreground;
    RcSession sess;
    RcRealtime realtime; int realtime_enabled; ar_ms realtime_sync_due;
    int have_sess;
    int interval;
    int outstanding;           /* events not yet returned by the GUI */
    int presence_ticks;
    ArTraffic traffic;
    int background_interval,cache_enabled,idle_polls;
    ar_ms rooms_due,presence_due,retry_due;
    char cache_origin[512],cache_dir[256];
    int retry_delay,retry_login; char retry_uid[64],retry_token[128];
    int net_trouble;           /* last poll failed */
    struct MsgPort *evreply;
    struct MsgPort *tport;
    struct timerequest *treq;
    int timer_open;
    int timer_pending;
} Worker;


static void post_status(Worker *w,int rc,const char *text);
static ar_ms network_now(void)
{
    struct DateStamp d; DateStamp(&d);
    return ((ar_ms)d.ds_Days*86400+(ar_ms)d.ds_Minute*60)*1000+(ar_ms)d.ds_Tick*20;
}
static ar_ms network_wall_clock(void *ctx)
{ (void)ctx; return network_now()+252460800000LL; }
static int network_before(void *ctx)
{
    Worker *w=ctx; ar_ms now=network_now(),wait;
    if(w->traffic.cooldown>now) return -1;
    wait=traffic_wait(&w->traffic,now);
    if(wait>0) Delay((ULONG)((wait+19)/20));
    traffic_start(&w->traffic,network_now()); return 0;
}
static void network_after(void *ctx,int status,int retry)
{
    Worker *w=ctx; ar_ms now=network_now();
    traffic_result(&w->traffic,now,status,retry);
    if(status>=200 && status<300 && w->net_trouble && w->sess.logged_in && !w->retry_login) {
        w->net_trouble=w->retry_delay=0; w->retry_due=0; post_status(w,RC_OK,"Connection restored");
    }
    if(status==0 || status==429 || status>=500) {
        char text[128]; w->net_trouble=1;
        snprintf(text,sizeof(text),"%s %d: all requests paused for %ld seconds",status?"HTTP":"Transport failure",status,(long)((w->traffic.cooldown-now+999)/1000));
        post_status(w,RC_ERR_HTTP,text);
    }
}
static int poll_interval(Worker *w)
{
    int n=w->interval;
    if(realtime_ready(&w->realtime) && n<60) n=60;
    if(!w->foreground || w->idle_polls>=6) { if(n<w->background_interval) n=w->background_interval; }
    return n;
}
/* Bounded persistent data cache, not configuration. 64 slots x 128 KiB;
 * the full key prevents hash collisions from returning another account's data. */
static int avatar_cache_at(Worker *w,const char *user,Buf *bytes,int save,const char *directory)
{
    char key[768],stored[768],path[96]; unsigned long hash=2166136261UL; BPTR f,dir;
    ar_ms stamp,now=network_now(); ULONG length; const unsigned char *p;
    if(!w->cache_enabled || !user || !w->sess.rc.user_id[0]) return 0;
    snprintf(key,sizeof(key),"%s|%s|%s",w->cache_origin,w->sess.rc.user_id,user);
    for(p=(const unsigned char *)key;*p;p++) hash=(hash^*p)*16777619UL;
    snprintf(path,sizeof(path),"%s/%02lu.dat",directory,hash%64);
    if(save) {
        if(!bytes->len || bytes->len>128UL*1024) return 0;
        if(!strncmp(directory,"PROGDIR:",8)) { dir=CreateDir("PROGDIR:Cache"); if(dir) UnLock(dir); }
        dir=CreateDir(directory); if(dir) UnLock(dir);
        f=Open(path,MODE_NEWFILE); if(!f) return 0;
        memset(stored,0,sizeof(stored)); ar_strlcpy(stored,key,sizeof(stored)); length=bytes->len;
        if(Write(f,stored,sizeof(stored))!=sizeof(stored) || Write(f,&now,sizeof(now))!=sizeof(now) ||
           Write(f,&length,sizeof(length))!=sizeof(length) || Write(f,bytes->data,length)!=(LONG)length) {
            Close(f); DeleteFile(path); return 0;
        }
        if(!Close(f)) { DeleteFile(path); return 0; } return 1;
    }
    f=Open(path,MODE_OLDFILE); if(!f) return 0;
    if(Read(f,stored,sizeof(stored))!=sizeof(stored) || memcmp(stored,key,strlen(key)+1) ||
       Read(f,&stamp,sizeof(stamp))!=sizeof(stamp) || stamp>now || now-stamp>86400000 ||
       Read(f,&length,sizeof(length))!=sizeof(length) || !length || length>128UL*1024) { Close(f); return 0; }
    buf_clear(bytes);
    { char chunk[2048]; ULONG left=length; int good=1;
      while(left) { LONG n=left>sizeof(chunk)?sizeof(chunk):left;
        if(Read(f,chunk,n)!=n || buf_append(bytes,chunk,n)<0) { good=0; break; } left-=n; }
      Close(f); if(!good) { buf_clear(bytes); return 0; }
    }
    w->traffic.cache_hits++; return 1;
}

static int avatar_cache(Worker *w,const char *user,Buf *bytes,int save)
{
    int result; struct Process *process=(struct Process *)FindTask(NULL); APTR window=process->pr_WindowPtr;
    if(prefs_lock()<0) return 0;
    process->pr_WindowPtr=(APTR)-1;
    { char directory[280]; BPTR root=0;
      if(save) {root=CreateDir(w->cache_dir[0]?w->cache_dir:"PROGDIR:Cache");if(root)UnLock(root);}
      ar_strlcpy(directory,w->cache_dir[0]?w->cache_dir:"PROGDIR:Cache",sizeof(directory));
      if(!AddPart(directory,"Avatars",sizeof(directory))) {process->pr_WindowPtr=window;prefs_unlock();return 0;}
      result=avatar_cache_at(w,user,bytes,save,directory); }
    process->pr_WindowPtr=window; prefs_unlock(); return result;
}

static void timer_start(Worker *w, int secs)
{
    if (!w->timer_open || w->timer_pending)
        return;
    w->treq->tr_node.io_Command = TR_ADDREQUEST;
    w->treq->tr_time.tv_secs = (ULONG)(secs > 0 ? secs : 1);
    w->treq->tr_time.tv_micro = 0;
    SendIO((struct IORequest *)w->treq);
    w->timer_pending = 1;
}

/* Port replies and commands must not cancel the shared wake timer: AbortIO
 * posts a reply signal that can otherwise feed the next Wait indefinitely. */
static void timer_keep_wake(Worker *w)
{
    if(w->slot==0) timer_start(w,1);
}

static void timer_stop(Worker *w)
{
    if (!w->timer_pending)
        return;
    if (!CheckIO((struct IORequest *)w->treq))
        AbortIO((struct IORequest *)w->treq);
    WaitIO((struct IORequest *)w->treq);
    w->timer_pending = 0;
}

static void post_event(Worker *w, NetMsg *e)
{
    e->server_slot=w->slot; e->server_generation=w->generation;
    e->msg.mn_ReplyPort = w->evreply;
    if (w->have_sess) ar_strlcpy(e->user_id, w->sess.rc.user_id, sizeof(e->user_id));
    e->server_now = w->have_sess ? sess_server_now(&w->sess) : 0;
    w->outstanding++;
    PutMsg(g_guiport, &e->msg);
}

static void post_status(Worker *w, int rc, const char *text)
{
    NetMsg *e = netmsg_new(NE_STATUS);
    if (!e)
        return;
    e->rc = rc;
    if(rc!=RC_OK && w->traffic.cooldown>network_now()) e->a_interval=(int)((w->traffic.cooldown-network_now()+999)/1000);
    ar_strlcpy(e->text, text, sizeof(e->text));
    post_event(w, e);
}

typedef struct { Worker *worker; ULONG serial; size_t reported; int started; } TransferProgress;
static int transfer_progress(void *ctx,size_t received,size_t total)
{
    TransferProgress *p=ctx;
    /* The worker cannot reach its outer event loop while HTTP is reading.
     * Reclaim GUI acknowledgements here, or progress stops after three posts. */
    if(p->worker->evreply) {
        NetMsg *reply;
        while((reply=(NetMsg *)GetMsg(p->worker->evreply))) {
            if(reply->server_slot>=0 && reply->server_slot<PREFS_SERVERS_MAX) {
                Worker *owner=p->worker->all_workers?&p->worker->all_workers[reply->server_slot]:p->worker;
                if(owner->outstanding>0)owner->outstanding--;
            }
            netmsg_free(reply);
        }
    }
    if(p->serial && transfer_cancel[p->worker->slot]==p->serial) return 1;
    if(p->worker->outstanding<3 && ((!p->started) || received-p->reported>=32768 || (total && received==total))) {
        p->started=1;
        NetMsg *e=netmsg_new(NE_TRANSFER);
        if(e) { e->a_before=received; e->a_interval=total; e->sent_at=p->serial; post_event(p->worker,e); }
        p->reported=received;
    }
    return 0;
}
static void drop_session(Worker *w)
{
    timer_stop(w);
    realtime_close(&w->realtime); w->realtime.retry_at=0; w->realtime.failures=0;
    if (w->have_sess)
        sess_cleanup(&w->sess);
    w->have_sess = 0;
    w->net_trouble = 0; w->retry_delay=w->retry_login=0;
    memset(w->retry_uid,0,sizeof(w->retry_uid)); memset(w->retry_token,0,sizeof(w->retry_token));
}

static void schedule_retry(Worker *w,int rc,const char *detail)
{
    NetMsg *e=netmsg_new(NE_STATUS);
    ar_ms remaining=w->traffic.cooldown-network_now();
    w->retry_delay=w->retry_delay? w->retry_delay*2:5;
    if(w->retry_delay>300) w->retry_delay=300;
    if(remaining>0 && (remaining+999)/1000>w->retry_delay) w->retry_delay=(int)((remaining+999)/1000);
    w->net_trouble=1; w->retry_due=network_now()+(ar_ms)w->retry_delay*1000;
    if(e) { e->rc=rc; e->a_interval=w->retry_delay;
        snprintf(e->text,sizeof(e->text),"Reconnecting in %d seconds: %.190s",w->retry_delay,detail);
        post_event(w,e); }
}
static int retryable_failure(const Worker *w,int rc)
{
    return rc==RC_ERR_NET || rc==RC_ERR_HTTP ||
        (rc==RC_ERR_SERVER && (w->sess.rc.last_status==429 || w->sess.rc.last_status>=500));
}
/* Never replay a send. Reconnect only the session/polling path. */
static void send_failure(Worker *w,int rc,const char *detail)
{
    if(rc==RC_ERR_AUTH) {
        realtime_close(&w->realtime);
        w->sess.logged_in=0; w->retry_login=w->retry_delay=w->net_trouble=0;
        memset(w->retry_token,0,sizeof(w->retry_token)); w->retry_uid[0]=0;
        timer_stop(w); post_status(w,rc,"Login expired; sign in again. Draft kept.");
    } else if(retryable_failure(w,rc)) {
        schedule_retry(w,rc,detail); timer_stop(w); timer_start(w,1);
    }
}
static void do_poll(Worker *w)
{
    NetMsg *e;
    int rc;

    if (!w->have_sess || !w->sess.logged_in)
        return;
    if (w->outstanding > 2)
        return;   /* GUI is busy; catch up on the next tick */
    e = netmsg_new(NE_UPDATE);
    if (!e)
        return;
    { ar_ms now=network_now(); int rooms=now>=w->rooms_due || w->realtime.dirty_rooms;
      rc = sess_poll(&w->sess, rooms?&e->rooms:NULL, &e->msgs);
      if(rc==RC_OK) {
          if(rooms) { w->rooms_due=now+(realtime_ready(&w->realtime)?120000:30000); w->realtime.dirty_rooms=0; }
          w->realtime.dirty_messages=0; w->realtime_sync_due=network_now()+2000;
      }
    }
    if(e->msgs.count || e->rooms.count || e->msgs.n_deleted || e->rooms.n_removed) w->idle_polls=0;
    else if(w->idle_polls<6) w->idle_polls++;
    ar_strlcpy(e->rid, w->sess.active_rid, sizeof(e->rid));
    if (rc != RC_OK) {
        char t[256];
        netmsg_free(e);
        if (rc == RC_ERR_AUTH) {
            snprintf(t, sizeof(t), "Logged out by the server: %s", rc_error(&w->sess.rc));
            send_failure(w,rc,t);
            return;
        }
        if(retryable_failure(w,rc)) schedule_retry(w,rc,rc_error(&w->sess.rc));
        else { realtime_close(&w->realtime); w->sess.logged_in=0; post_status(w,rc,rc_error(&w->sess.rc)); }
        return;
    }
    if (w->net_trouble) {
        w->net_trouble = 0; w->retry_delay=0;
        post_status(w, RC_OK, "Connection restored");
    }
    if (w->foreground && network_now()>=w->presence_due && w->traffic.cooldown<=network_now()) {
        NetMsg *presence = netmsg_new(NE_PRESENCE);
        w->presence_due=network_now()+60000;
        if (presence) {
            presence->rc = rc_presence(&w->sess.rc, &presence->members);
            post_event(w, presence);
        }
    }
    if (e->rooms.count || e->rooms.n_removed || e->msgs.count || e->msgs.n_deleted)
        post_event(w, e);
    else
        netmsg_free(e);
}

static void fill_login_result(Worker *w, NetMsg *m)
{
    RcClient *c = &w->sess.rc;

    ar_strlcpy(m->username, c->username, sizeof(m->username));
    ar_strlcpy(m->display_name, c->display_name, sizeof(m->display_name));
    ar_strlcpy(m->user_id, c->user_id, sizeof(m->user_id));
    ar_strlcpy(m->token, c->token, sizeof(m->token));
    ar_strlcpy(m->server_version, c->server_version, sizeof(m->server_version));
    ar_strlcpy(m->totp_method, c->totp_method, sizeof(m->totp_method));
}

typedef struct {BPTR file;int failed;} DownloadSink;
static int download_write(void *ctx,const void *data,size_t size)
{
    DownloadSink *sink=ctx;
    if(Write(sink->file,(APTR)data,(LONG)size)!=(LONG)size) {sink->failed=1;return -1;}
    return 0;
}
static int upload_cancel_only(void *ctx,size_t received,size_t total)
{ TransferProgress *p=ctx; (void)received; (void)total; return p->serial && transfer_cancel[p->worker->slot]==p->serial; }
static int upload_file_read(void *ctx,void *buffer,int count) { return Read((BPTR)ctx,buffer,count); }
static void handle(Worker *w, NetMsg *m)
{
    m->rc = RC_OK;
    m->text[0] = 0;
    if(m->kind!=NC_CONNECT && m->kind!=NC_DISCONNECT && m->kind!=NC_SET_INTERVAL && m->kind!=NC_SET_FOCUS && m->server_generation!=w->generation) {
        m->rc=RC_ERR_ARGS; ar_strlcpy(m->text,"Server profile changed",sizeof(m->text)); return;
    }
    if((m->kind==NC_UPLOAD || m->kind==NC_DOWNLOAD || m->kind==NC_IMAGE) && m->sent_at && transfer_cancel[w->slot]==(ULONG)m->sent_at) {
        m->rc=RC_ERR_ARGS; ar_strlcpy(m->text,"Transfer cancelled",sizeof(m->text)); return;
    }
    switch (m->kind) {
    case NC_CONNECT:
        if(strcmp(w->cache_origin,m->a_url?m->a_url:"")) { int spacing=w->traffic.spacing_ms; traffic_init(&w->traffic,w->traffic.seed); w->traffic.spacing_ms=spacing; }
        drop_session(w);
        w->generation=m->server_generation;
        m->rc = sess_init(&w->sess, net_amiga(), m->a_url ? m->a_url : "", m->a_verify);
        if (m->rc != RC_OK) {
            ar_strlcpy(m->text, rc_error(&w->sess.rc), sizeof(m->text));
            sess_cleanup(&w->sess);
            break;
        }
        w->have_sess = 1; w->sess.foreground=0;
        w->sess.rc.http.before_request=network_before; w->sess.rc.http.after_request=network_after;
        w->sess.rc.http.scheduler_ctx=w; w->sess.rc.http.wall_clock=network_wall_clock;
        ar_strlcpy(w->cache_origin,m->a_url?m->a_url:"",sizeof(w->cache_origin));
        if(m->a_uid && m->a_token) { ar_strlcpy(w->retry_uid,m->a_uid,sizeof(w->retry_uid)); ar_strlcpy(w->retry_token,m->a_token,sizeof(w->retry_token)); }
        m->rc = sess_login(&w->sess, m->a_user, m->a_pass, m->a_code, m->a_uid, m->a_token);
        fill_login_result(w, m);
        /* Password login may succeed before the initial rooms request fails.
         * Retain only its issued token for recovery, never the password. */
        if(m->rc!=RC_OK && w->sess.rc.user_id[0] && w->sess.rc.token[0]) {
            ar_strlcpy(w->retry_uid,w->sess.rc.user_id,sizeof(w->retry_uid));
            ar_strlcpy(w->retry_token,w->sess.rc.token,sizeof(w->retry_token));
        }
        if (m->rc == RC_OK) {
            w->rooms_due=network_now()+30000; w->idle_polls=0;
            rc_roomlist_copy(&m->rooms, &w->sess.rooms);
            timer_start(w, 1);
        } else {
            ar_strlcpy(m->text, rc_error(&w->sess.rc), sizeof(m->text));
            if(retryable_failure(w,m->rc) && w->retry_uid[0] && w->retry_token[0]) {
                w->retry_login=1; schedule_retry(w,m->rc,m->text); timer_start(w,1);
            } else if (!w->sess.logged_in) drop_session(w);
        }
        break;

    case NC_CREATE_ROOM:
        if (!w->have_sess || !w->sess.logged_in) { m->rc = RC_ERR_AUTH; break; }
        m->rc = rc_create_room(&w->sess.rc, m->a_user ? m->a_user[0] : 0, m->a_text, m->a_pass, &m->rooms);
        if (m->rc == RC_OK && m->rooms.count) {
            RcRoomList copy; rc_roomlist_init(&copy);
            ar_strlcpy(m->rid, m->rooms.items[0].rid, sizeof(m->rid));
            if (!rc_roomlist_copy(&copy, &m->rooms)) rc_roomlist_merge(&w->sess.rooms, &copy);
            rc_roomlist_free(&copy);
        } else ar_strlcpy(m->text, rc_error(&w->sess.rc), sizeof(m->text));
        break;
    case NC_IMAGE: {
        TransferProgress progress={w,(ULONG)m->sent_at,0,0};
        w->sess.rc.http.transfer_limit=m->a_before>=256UL*1024 && m->a_before<=2UL*1024*1024?(size_t)m->a_before:2UL*1024UL*1024UL;
        if(m->sent_at) { w->sess.rc.http.progress=transfer_progress; w->sess.rc.http.progress_ctx=&progress; }
        m->rc=w->have_sess && w->sess.logged_in?rc_download(&w->sess.rc,m->a_url,&m->avatar):RC_ERR_AUTH;
        w->sess.rc.http.transfer_limit=0; w->sess.rc.http.progress=NULL; w->sess.rc.http.progress_ctx=NULL;
        if(m->rc!=RC_OK) ar_strlcpy(m->text,rc_error(&w->sess.rc),sizeof(m->text));
        if(m->sent_at && transfer_cancel[w->slot]==(ULONG)m->sent_at) { m->rc=RC_ERR_ARGS; ar_strlcpy(m->text,"Download cancelled",sizeof(m->text)); }
        break;
    }

    case NC_OPEN_ROOM:
        if (!w->have_sess || !w->sess.logged_in) {
            m->rc = RC_ERR_AUTH;
            ar_strlcpy(m->text, "Not connected", sizeof(m->text));
            break;
        }
        ar_strlcpy(m->rid, m->a_rid ? m->a_rid : "", sizeof(m->rid));
        w->idle_polls=0;
        m->rc = sess_open_room(&w->sess, m->rid, &m->msgs);
        ar_strlcpy(m->topic, w->sess.active_topic, sizeof(m->topic));
        m->members = w->sess.active_members ? ar_strdup(w->sess.active_members) : NULL;
        if (m->rc != RC_OK)
            ar_strlcpy(m->text, m->rc == RC_ERR_ARGS ? "Unknown room" : rc_error(&w->sess.rc), sizeof(m->text));
        break;

    case NC_PRESENCE:
        m->rc = w->have_sess && w->sess.logged_in ? rc_presence(&w->sess.rc, &m->members) : RC_ERR_AUTH;
        break;
    case NC_AVATAR:
        if(!w->have_sess || !w->sess.logged_in) m->rc=RC_ERR_AUTH;
        else if(!avatar_cache(w,m->a_user,&m->avatar,0)) {
            m->rc=rc_avatar(&w->sess.rc,m->a_user,&m->avatar);
            if(m->rc==RC_OK) avatar_cache(w,m->a_user,&m->avatar,1);
        }
        break;
    case NC_HISTORY: {
        const RcRoom *room;
        if (!w->have_sess || !w->sess.logged_in) { m->rc = RC_ERR_AUTH; break; }
        ar_strlcpy(m->rid, m->a_rid ? m->a_rid : "", sizeof(m->rid));
        room = sess_find_room(&w->sess, m->rid);
        m->rc = room ? rc_history(&w->sess.rc, m->rid, room->type, 50, m->a_before, &m->msgs) : RC_ERR_ARGS;
        if (m->rc != RC_OK) ar_strlcpy(m->text, rc_error(&w->sess.rc), sizeof(m->text));
        break;
    }
    case NC_DOWNLOAD: {
        TransferProgress progress={w,(ULONG)m->sent_at,0};
        BPTR f;
        if (!w->have_sess || !w->sess.logged_in) { m->rc = RC_ERR_AUTH; break; }
        char part[600],backup[600];BPTR existing;int had_old=0,staged=0;DownloadSink sink={0};
        snprintf(part,sizeof(part),"%s.ar-part-%08lx",m->a_text,(unsigned long)FindTask(NULL));
        snprintf(backup,sizeof(backup),"%s.ar-backup-%08lx",m->a_text,(unsigned long)FindTask(NULL));
        existing=Lock(part,ACCESS_READ);if(existing){UnLock(existing);f=0;}else f=Open(part,MODE_NEWFILE);
        if(!f) {m->rc=RC_ERR_ARGS;ar_strlcpy(m->text,"Cannot create a temporary download file",sizeof(m->text));break;}
        sink.file=f;
        w->sess.rc.http.progress=transfer_progress;w->sess.rc.http.progress_ctx=&progress;
        w->sess.rc.http.transfer_limit=m->a_before>0 && m->a_before<=8UL*1024*1024?(size_t)m->a_before:0;
        m->rc=rc_download_to(&w->sess.rc,m->a_url,download_write,&sink);
        w->sess.rc.http.transfer_limit=0;w->sess.rc.http.progress=NULL;w->sess.rc.http.progress_ctx=NULL;
        if(!Close(f) || sink.failed) {m->rc=RC_ERR_ARGS;ar_strlcpy(m->text,"Could not write the complete file; original kept",sizeof(m->text));}
        if(progress.serial && transfer_cancel[w->slot]==progress.serial) {m->rc=RC_ERR_ARGS;ar_strlcpy(m->text,"Download cancelled",sizeof(m->text));}
        if(m->rc==RC_OK)staged=1;else {DeleteFile(part);if(!m->text[0])ar_strlcpy(m->text,rc_error(&w->sess.rc),sizeof(m->text));}
            if (staged) {
                existing = Lock(m->a_text, ACCESS_READ);
                if (existing) {
                    struct FileInfoBlock *info = AllocDosObject(DOS_FIB, NULL);
                    int directory = !info || !Examine(existing, info) || info->fib_DirEntryType >= 0;
                    if (info) FreeDosObject(DOS_FIB, info);
                    UnLock(existing);
                    if (directory) m->rc = RC_ERR_ARGS;
                    else {
                        existing = Lock(backup, ACCESS_READ);
                        if (existing) { UnLock(existing); m->rc = RC_ERR_ARGS; }
                        else if (Rename(m->a_text, backup)) had_old = 1;
                        else m->rc = RC_ERR_ARGS;
                    }
                }
                if (m->rc == RC_OK && !Rename(part, m->a_text)) m->rc = RC_ERR_ARGS;
                if (m->rc != RC_OK) {
                    if (had_old) Rename(backup, m->a_text);
                    DeleteFile(part);
                    ar_strlcpy(m->text, "Could not finish saving; original file or backup kept", sizeof(m->text));
                } else if (had_old && !DeleteFile(backup)) {
                    ar_strlcpy(m->text, "Saved; a backup of the previous file remains beside it", sizeof(m->text));
                }
            }

        break;
    }

    case NC_SEND:
        ar_strlcpy(m->rid,m->a_rid?m->a_rid:"",sizeof(m->rid));
        w->idle_polls=0;
        if (!w->have_sess || !w->sess.logged_in) {
            m->rc = RC_ERR_AUTH;
            ar_strlcpy(m->text, "Not connected", sizeof(m->text));
            break;
        }
        ar_strlcpy(m->rid, m->a_rid ? m->a_rid : "", sizeof(m->rid));
        if(m->a_uid && m->a_verify){
            m->rc=rc_confirm_send(&w->sess.rc,m->rid,m->a_text,m->a_uid,&m->msgs);
            if(m->rc==RC_OK)break;
            if(!(m->rc==RC_ERR_SERVER && (w->sess.rc.last_status==400 || w->sess.rc.last_status==404))){ar_strlcpy(m->text,"Could not confirm delivery; draft kept. Retry when connected.",sizeof(m->text));break;}
        }
        m->rc=m->a_uid?rc_send_id(&w->sess.rc,m->rid,m->a_text,m->a_uid,&m->msgs):rc_send(&w->sess.rc,m->rid,m->a_text,&m->msgs);
        if (m->rc != RC_OK) {
            ar_strlcpy(m->text, rc_error(&w->sess.rc), sizeof(m->text));
            send_failure(w,m->rc,m->text);
        }
        break;

    case NC_THREAD: case NC_EDIT: case NC_DELETE: case NC_REACT: case NC_THREAD_SEND:
        ar_strlcpy(m->rid,m->a_rid?m->a_rid:"",sizeof(m->rid));
        if(!w->have_sess || !w->sess.logged_in) m->rc=RC_ERR_AUTH;
        else if(m->kind==NC_THREAD) m->rc=rc_thread(&w->sess.rc,m->a_uid,m->a_interval,&m->msgs,&m->a_background);
        else if(m->kind==NC_EDIT) m->rc=rc_message_edit(&w->sess.rc,m->rid,m->a_uid,m->a_text,&m->msgs);
        else if(m->kind==NC_DELETE) m->rc=rc_message_delete(&w->sess.rc,m->rid,m->a_uid);
        else if(m->kind==NC_REACT) m->rc=rc_message_react(&w->sess.rc,m->a_uid,m->a_text,m->a_verify);
        else m->rc=rc_send_thread(&w->sess.rc,m->rid,m->a_uid,m->a_text,&m->msgs);
        if(m->rc!=RC_OK) ar_strlcpy(m->text,rc_error(&w->sess.rc),sizeof(m->text));
        break;

    case NC_SEARCH:
        ar_strlcpy(m->rid,m->a_rid?m->a_rid:"",sizeof(m->rid));
        m->rc=w->have_sess && w->sess.logged_in?rc_search(&w->sess.rc,m->rid,m->a_text,m->a_interval,25,&m->msgs,&m->a_background):RC_ERR_AUTH;
        if(m->rc!=RC_OK) ar_strlcpy(m->text,rc_error(&w->sess.rc),sizeof(m->text));
        break;
    case NC_UPLOAD: {
        BPTR file=0; struct FileInfoBlock *info=NULL; char file_id[96]; TransferProgress progress={w,(ULONG)m->sent_at,0,0};
        ar_strlcpy(m->rid,m->a_url?m->a_url:"",sizeof(m->rid));
        if(!w->have_sess || !w->sess.logged_in) { m->rc=RC_ERR_AUTH; break; }
        if(!m->a_text || !m->a_url) { m->rc=RC_ERR_ARGS; break; }
        file=Open(m->a_text,MODE_OLDFILE); info=AllocDosObject(DOS_FIB,NULL);
        if(!file || !info || !ExamineFH(file,info) || info->fib_DirEntryType>=0 || info->fib_Size<0 || info->fib_Size>8*1024*1024) {
            m->rc=RC_ERR_ARGS; ar_strlcpy(m->text,"Choose a readable file of at most 8 MiB",sizeof(m->text));
        } else {
            w->sess.rc.http.progress=transfer_progress; w->sess.rc.http.progress_ctx=&progress;
            { Buf filename; buf_init(&filename); latin1_to_utf8(&filename,FilePart(m->a_text));
              m->rc=filename.oom?RC_ERR_NOMEM:rc_upload(&w->sess.rc,m->rid,buf_str(&filename),info->fib_Size,upload_file_read,(void *)file,file_id);
              buf_free(&filename); }
            w->sess.rc.http.progress=upload_cancel_only;
            if(m->rc==RC_OK && transfer_cancel[w->slot]!=(ULONG)m->sent_at) m->rc=rc_upload_confirm(&w->sess.rc,m->rid,file_id);
            else if(m->rc==RC_OK) m->rc=RC_ERR_ARGS;
            w->sess.rc.http.progress=NULL; w->sess.rc.http.progress_ctx=NULL;
            if(m->rc!=RC_OK) ar_strlcpy(m->text,rc_error(&w->sess.rc),sizeof(m->text));
        }
        if(file) Close(file);
        if(info) FreeDosObject(DOS_FIB,info);
        break;
    }
    case NC_POLL_NOW:
        do_poll(w);
        break;

    case NC_SET_FOCUS:
        w->idle_polls=0;
        w->foreground=m->a_interval!=0; w->sess.foreground=0;
        if(w->foreground && w->have_sess && w->sess.logged_in && m->a_rid && !strcmp(m->a_rid,w->sess.active_rid)) rc_mark_read(&w->sess.rc,w->sess.active_rid);
        break;

    case NC_SET_INTERVAL:
        if(m->a_before>=100 && m->a_before<=5000) w->traffic.spacing_ms=(int)m->a_before;
        if(m->a_background>=15 && m->a_background<=600) w->background_interval=m->a_background;
        w->cache_enabled=m->a_verify!=0;
        if(m->a_text && *m->a_text)ar_strlcpy(w->cache_dir,m->a_text,sizeof(w->cache_dir));
        if(w->realtime_enabled!=(m->a_realtime!=0)) {
            realtime_close(&w->realtime); w->realtime.retry_at=0; w->realtime.failures=0;
        }
        w->realtime_enabled=m->a_realtime!=0;
        if(!w->realtime_enabled) { realtime_close(&w->realtime); }
        w->interval = m->a_interval < 2 ? 2 : m->a_interval > 600 ? 600 : m->a_interval;
        break;

    case NC_LOGOUT:
        if (w->have_sess && w->sess.logged_in)
            m->rc = rc_logout(&w->sess.rc);
        drop_session(w);
        break;

    case NC_DISCONNECT:
        drop_session(w);
        w->generation=m->server_generation;
        break;
    }
    if(m->rc==RC_ERR_AUTH && (m->kind==NC_SEARCH || m->kind==NC_UPLOAD) && w->have_sess && w->sess.logged_in) send_failure(w,m->rc,m->text[0]?m->text:"Authentication expired");
    if (w->have_sess) ar_strlcpy(m->user_id, w->sess.rc.user_id, sizeof(m->user_id));
    m->server_now = w->have_sess ? sess_server_now(&w->sess) : 0;
}

static void worker_release(Worker *w)
{
    drop_session(w);
    if(w->timer_open) CloseDevice((struct IORequest *)w->treq);
    if(w->treq) DeleteIORequest((struct IORequest *)w->treq);
    if(w->tport) DeleteMsgPort(w->tport);
}
#include "network_queue.inc"
#ifdef AR_WORKER_QA
static volatile ULONG qa_worker_wakes;
#endif
static void worker_entry(void)
{
    Worker *workers=ar_calloc(PREFS_SERVERS_MAX,sizeof(Worker));
    struct MsgPort *events=CreateMsgPort(); NetMsg *quit=NULL;
    ULONG cmdsig,evsig,mask=0; int i,ok=workers && events;
    g_cmdport=CreateMsgPort(); if(!g_cmdport) ok=0;
    for(i=0;ok && i<PREFS_SERVERS_MAX;i++) {
        Worker *w=&workers[i]; w->slot=i; w->all_workers=workers; w->interval=5; w->evreply=events;
        traffic_init(&w->traffic,(ULONG)FindTask(NULL)+(ULONG)i*65537+network_now());
        w->background_interval=30; w->cache_enabled=1;
        w->tport=CreateMsgPort();
        if(w->tport) w->treq=(struct timerequest *)CreateIORequest(w->tport,sizeof(struct timerequest));
        if(w->treq && !OpenDevice((CONST_STRPTR)TIMERNAME,UNIT_VBLANK,(struct IORequest *)w->treq,0)) w->timer_open=1;
        if(!w->timer_open) ok=0;
        else mask|=1UL<<w->tport->mp_SigBit;
    }
    if(!ok) {
        if(workers) { for(i=0;i<PREFS_SERVERS_MAX;i++) worker_release(&workers[i]); ar_free(workers); }
        if(events) DeleteMsgPort(events);
        if(g_cmdport) DeleteMsgPort(g_cmdport);
        g_cmdport=NULL;
        ar_strlcpy(g_starterr,"Cannot set up server sessions (ports/timers)",sizeof(g_starterr));
        Forbid(); g_started=-1; Signal(g_parent,1UL<<g_startsig); return;
    }
    g_started=1; Signal(g_parent,1UL<<g_startsig);
    cmdsig=1UL<<g_cmdport->mp_SigBit; evsig=1UL<<events->mp_SigBit;
    mask|=cmdsig|evsig|SIGBREAKF_CTRL_C;
    { NetMsg *queue[64]; int count=0; ar_ms poll_due[PREFS_SERVERS_MAX]={0},diag_due[PREFS_SERVERS_MAX]={0};
    /* One wake timer also services commands deferred by a server cooldown. */
    timer_start(&workers[0],1);
    while(!quit) {
        ULONG sigs=Wait(mask); struct Message *msg; int j;
#ifdef AR_WORKER_QA
        qa_worker_wakes++;
#endif
        for(i=0;i<PREFS_SERVERS_MAX;i++) {
            Worker *w=&workers[i];
            if((sigs&(1UL<<w->tport->mp_SigBit)) && w->timer_pending && CheckIO((struct IORequest *)w->treq)) {
                WaitIO((struct IORequest *)w->treq); w->timer_pending=0;
            }
        }
        while((msg=GetMsg(events))) {
            NetMsg *m=(NetMsg *)msg;
            if(m->server_slot>=0 && m->server_slot<PREFS_SERVERS_MAX) workers[m->server_slot].outstanding--;
            netmsg_free(m);
        }
        while((msg=GetMsg(g_cmdport))) {
            NetMsg *m=(NetMsg *)msg; int duplicate=0;
            if(m->kind==NC_QUIT) { quit=m; break; }
            for(j=0;j<count;j++) if(duplicate_command(queue[j],m)) duplicate=1;
            if(duplicate || count==64) {
                m->rc=duplicate?RC_OK:RC_ERR_ARGS;
                if(!duplicate) ar_strlcpy(m->text,"Network queue full; try again",sizeof(m->text));
                ReplyMsg(&m->msg);
            } else queue[count++]=m;
        }
        /* At most one queued operation between timer/port drains. */
        if(!quit && count) {
            int chosen=-1,priority=99;
            for(j=0;j<count;j++) {
                NetMsg *m=queue[j]; int p=command_priority(m->kind),slot=m->server_slot;
                if(slot>=0 && slot<PREFS_SERVERS_MAX && (m->kind==NC_UPLOAD || m->kind==NC_DOWNLOAD || m->kind==NC_IMAGE) && m->sent_at && transfer_cancel[slot]==(ULONG)m->sent_at) p=0;
                if(p && slot>=0 && slot<PREFS_SERVERS_MAX && workers[slot].traffic.cooldown>network_now() &&
                   (m->kind==NC_CONNECT ? !strcmp(workers[slot].cache_origin,m->a_url?m->a_url:"") : m->server_generation==workers[slot].generation && workers[slot].have_sess && (workers[slot].sess.logged_in || workers[slot].retry_login))) continue;
                if(p<priority) { chosen=j; priority=p; }
            }
            if(chosen>=0) {
                NetMsg *m=queue[chosen];
                for(j=chosen+1;j<count;j++) queue[j-1]=queue[j];
                count--;
                if(m->kind==NC_SET_INTERVAL) for(i=0;i<PREFS_SERVERS_MAX;i++) handle(&workers[i],m);
                else if(m->server_slot>=0 && m->server_slot<PREFS_SERVERS_MAX) handle(&workers[m->server_slot],m);
                else m->rc=RC_ERR_ARGS;
                if(m->kind==NC_DISCONNECT || m->kind==NC_LOGOUT) {
                    int older=chosen; /* entries originally queued before this barrier */
                    for(j=0;j<count;) {
                        NetMsg *pending=queue[j];
                        if(cancel_after_disconnect(pending,m,j<older)) {
                            int k; if(j<older) older--; for(k=j+1;k<count;k++) queue[k-1]=queue[k]; count--;
                            pending->rc=RC_ERR_NET; ar_strlcpy(pending->text,"Disconnected; request not sent",sizeof(pending->text)); ReplyMsg(&pending->msg);
                        } else j++;
                    }
                }
                if(m->kind==NC_OPEN_ROOM || m->kind==NC_SET_FOCUS) {
                    if(m->server_slot>=0 && m->server_slot<PREFS_SERVERS_MAX) poll_due[m->server_slot]=network_now();
                }
                ReplyMsg(&m->msg);
            }
        }
        for(i=0;!quit && i<PREFS_SERVERS_MAX;i++) {
            Worker *w=&workers[i]; ar_ms now=network_now(); int queued=0,urgent=0;
            for(j=0;j<count;j++) if(queue[j]->server_slot==i) { queued++; if(command_priority(queue[j]->kind)<3) urgent=1; }
            if(w->realtime_enabled && w->have_sess && w->sess.logged_in && !w->retry_login) {
                RcRealtime *r=&w->realtime; int was_live=realtime_ready(r);
                if(!r->phase && !urgent && now>=r->retry_at && now>=w->traffic.cooldown && !traffic_wait(&w->traffic,now)) {
                    traffic_start(&w->traffic,now); /* handshake shares the REST request budget */
                    if(realtime_start(r,&w->sess.rc,now)) {
                        if(r->ws.status==429 || r->ws.status==503) {
                            traffic_result(&w->traffic,network_now(),r->ws.status,r->ws.retry_after);
                            if(r->retry_at<w->traffic.cooldown) r->retry_at=w->traffic.cooldown;
                        }
                        if(!w->net_trouble) post_status(w,RC_OK,"Realtime unavailable; using polling (automatic retry)");
                    }
                }
                if(r->phase && realtime_step(r,&w->sess.rc,w->sess.active_rid,network_now())) {
                    if(!w->net_trouble) post_status(w,RC_OK,"Realtime interrupted; polling will catch up");
                    poll_due[i]=network_now();
                }
                if(realtime_ready(r) && !was_live && !w->net_trouble) post_status(w,RC_OK,"Realtime connected; live event subscriptions active");
                if(!w->net_trouble && (r->dirty_messages || r->dirty_rooms) && now>=w->realtime_sync_due && poll_due[i]>now) poll_due[i]=now;
            }
            if(now>=diag_due[i] && w->cache_origin[0]) { network_snapshot(w,queued); diag_due[i]=now+5000; }
            if(!w->net_trouble && !w->retry_login && poll_due[i]>now+(ar_ms)poll_interval(w)*1000) poll_due[i]=now+(ar_ms)poll_interval(w)*1000;
            if(!urgent && w->have_sess && now>=poll_due[i] && now>=w->traffic.cooldown) {
                if(w->retry_login) {
                    int rc=sess_login(&w->sess,NULL,NULL,NULL,w->retry_uid,w->retry_token);
                    if(rc==RC_OK) {
                        NetMsg *e=netmsg_new(NE_RECONNECTED); w->retry_login=w->retry_delay=w->net_trouble=0;
                        if(e) { e->rc=RC_OK; fill_login_result(w,e); rc_roomlist_copy(&e->rooms,&w->sess.rooms); post_event(w,e); }
                    } else if(retryable_failure(w,rc)) schedule_retry(w,rc,rc_error(&w->sess.rc));
                    else { w->retry_login=0; send_failure(w,rc,rc_error(&w->sess.rc)); }
                } else if(w->sess.logged_in) do_poll(w);
                poll_due[i]=network_now()+(ar_ms)(w->retry_delay?w->retry_delay:poll_interval(w))*1000;
            }
            /* Leave pending timers alone. Completion is drained above; only
             * the shared heartbeat is rearmed. Commands must not reset it. */
            timer_keep_wake(w);
        }
    }
    while(count) { NetMsg *m=queue[--count]; m->rc=RC_ERR_NET; ar_strlcpy(m->text,"Client stopped; request not sent",sizeof(m->text)); ReplyMsg(&m->msg); }
    }
    for(i=0;i<PREFS_SERVERS_MAX;i++) drop_session(&workers[i]);
    /* A shared reply port can receive events from any server during shutdown. */
    for(;;) {
        int outstanding=0; struct Message *msg;
        for(i=0;i<PREFS_SERVERS_MAX;i++) outstanding+=workers[i].outstanding;
        if(!outstanding) break;
        WaitPort(events);
        while((msg=GetMsg(events))) { NetMsg *m=(NetMsg *)msg; workers[m->server_slot].outstanding--; netmsg_free(m); }
    }
    for(i=0;i<PREFS_SERVERS_MAX;i++) worker_release(&workers[i]);
    ar_free(workers); net_amiga_close(); DeleteMsgPort(events);
    {
        struct Message *msg;
        while((msg=GetMsg(g_cmdport))) { ((NetMsg *)msg)->rc=RC_ERR_NET; ReplyMsg(msg); }
    }
    Forbid(); DeleteMsgPort(g_cmdport); g_cmdport=NULL; ReplyMsg(&quit->msg);
}

int worker_start(struct MsgPort *gui_port, char *err, int errsz)
{
    struct Process *p;

    if (g_worker)
        return 0;
    g_guiport = gui_port;
    g_parent = FindTask(NULL);
    g_startsig = AllocSignal(-1);
    if (g_startsig < 0) {
        ar_strlcpy(err, "No free signal", (size_t)errsz);
        return -1;
    }
    g_started = 0;
    p = CreateNewProcTags(NP_Entry, (ULONG)worker_entry,
                          NP_Name, (ULONG)"AmiRocket network",
                          NP_StackSize, WORKER_STACK,
                          NP_Priority, 0,
                          NP_Output, 0,
                          NP_Input, 0,
                          NP_CloseOutput, FALSE,
                          NP_CloseInput, FALSE,
                          TAG_END);
    if (!p) {
        FreeSignal(g_startsig);
        g_startsig = -1;
        ar_strlcpy(err, "Cannot start the network process", (size_t)errsz);
        return -1;
    }
    while (!g_started)
        Wait(1UL << g_startsig);
    FreeSignal(g_startsig);
    g_startsig = -1;
    if (g_started < 0) {
        ar_strlcpy(err, g_starterr, (size_t)errsz);
        return -1;
    }
    g_worker = &p->pr_Task;
    return 0;
}

void worker_stop(void)
{
    struct MsgPort *tmp;
    NetMsg *q;
    int done = 0;

    if (!g_worker)
        return;
    tmp = CreateMsgPort();
    q = netmsg_new(NC_QUIT);
    if (!tmp || !q) {
        /* cannot shut down cleanly; leave the process running rather than crash */
        if (tmp)
            DeleteMsgPort(tmp);
        ar_free(q);
        return;
    }
    Signal(g_worker, SIGBREAKF_CTRL_C);   /* abort a blocking network call */
    q->msg.mn_ReplyPort = tmp;
    PutMsg(g_cmdport, &q->msg);
    while (!done) {
        struct Message *msg;
        Wait((1UL << tmp->mp_SigBit) | (1UL << g_guiport->mp_SigBit));
        while ((msg = GetMsg(g_guiport)) != NULL) {
            NetMsg *m = (NetMsg *)msg;
            if (msg->mn_Node.ln_Type == NT_REPLYMSG)
                netmsg_free(m);
            else
                worker_done(m);
        }
        if (GetMsg(tmp))
            done = 1;
    }
    netmsg_free(q);
    DeleteMsgPort(tmp);
    g_worker = NULL;
}

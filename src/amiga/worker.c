#include "worker.h"
#include "prefs.h"
#include "net_amiga.h"
#include "../core/mem.h"
#include "../core/session.h"
#include "../core/util.h"

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dostags.h>
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
typedef struct {
    int slot, generation,foreground;
    RcSession sess;
    int have_sess;
    int interval;
    int outstanding;           /* events not yet returned by the GUI */
    int presence_ticks;
    int retry_delay,retry_login; char retry_uid[64],retry_token[128];
    int net_trouble;           /* last poll failed */
    struct MsgPort *evreply;
    struct MsgPort *tport;
    struct timerequest *treq;
    int timer_open;
    int timer_pending;
} Worker;

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
    ar_strlcpy(e->text, text, sizeof(e->text));
    post_event(w, e);
}

typedef struct { Worker *worker; ULONG serial; size_t reported; int started; } TransferProgress;
static int transfer_progress(void *ctx,size_t received,size_t total)
{
    TransferProgress *p=ctx;
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
    if (w->have_sess)
        sess_cleanup(&w->sess);
    w->have_sess = 0;
    w->net_trouble = 0; w->retry_delay=w->retry_login=0;
    memset(w->retry_uid,0,sizeof(w->retry_uid)); memset(w->retry_token,0,sizeof(w->retry_token));
}

static void schedule_retry(Worker *w,int rc,const char *detail)
{
    NetMsg *e=netmsg_new(NE_STATUS);
    w->retry_delay=w->retry_delay? w->retry_delay*2:5;
    if(w->retry_delay>300) w->retry_delay=300;
    w->net_trouble=1;
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
        w->sess.logged_in=0; w->retry_login=w->retry_delay=w->net_trouble=0;
        memset(w->retry_token,0,sizeof(w->retry_token)); w->retry_uid[0]=0;
        timer_stop(w); post_status(w,rc,"Login expired; sign in again. Draft kept.");
    } else if(retryable_failure(w,rc)) {
        schedule_retry(w,rc,detail); timer_stop(w); timer_start(w,w->retry_delay);
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
    rc = sess_poll(&w->sess, &e->rooms, &e->msgs);
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
        else { w->sess.logged_in=0; post_status(w,rc,rc_error(&w->sess.rc)); }
        return;
    }
    if (w->net_trouble) {
        w->net_trouble = 0; w->retry_delay=0;
        post_status(w, RC_OK, "Connection restored");
    }
    w->presence_ticks += w->interval;
    if (w->presence_ticks >= 30) {
        NetMsg *presence = netmsg_new(NE_PRESENCE);
        w->presence_ticks = 0;
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

static void handle(Worker *w, NetMsg *m)
{
    m->rc = RC_OK;
    m->text[0] = 0;
    if(m->kind!=NC_CONNECT && m->kind!=NC_DISCONNECT && m->kind!=NC_SET_INTERVAL && m->kind!=NC_SET_FOCUS && m->server_generation!=w->generation) {
        m->rc=RC_ERR_ARGS; ar_strlcpy(m->text,"Server profile changed",sizeof(m->text)); return;
    }
    switch (m->kind) {
    case NC_CONNECT:
        drop_session(w);
        w->generation=m->server_generation;
        m->rc = sess_init(&w->sess, net_amiga(), m->a_url ? m->a_url : "", m->a_verify);
        if (m->rc != RC_OK) {
            ar_strlcpy(m->text, rc_error(&w->sess.rc), sizeof(m->text));
            sess_cleanup(&w->sess);
            break;
        }
        w->have_sess = 1; w->sess.foreground=0;
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
            rc_roomlist_copy(&m->rooms, &w->sess.rooms);
            timer_start(w, w->interval);
        } else {
            ar_strlcpy(m->text, rc_error(&w->sess.rc), sizeof(m->text));
            if(retryable_failure(w,m->rc) && w->retry_uid[0] && w->retry_token[0]) {
                w->retry_login=1; schedule_retry(w,m->rc,m->text); timer_start(w,w->retry_delay);
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
        w->sess.rc.http.transfer_limit=2UL*1024UL*1024UL;
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
        m->rc = w->have_sess && w->sess.logged_in ? rc_avatar(&w->sess.rc, m->a_user, &m->avatar) : RC_ERR_AUTH;
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
        Buf data; TransferProgress progress={w,(ULONG)m->sent_at,0};
        BPTR f;
        if (!w->have_sess || !w->sess.logged_in) { m->rc = RC_ERR_AUTH; break; }
        buf_init(&data);
        w->sess.rc.http.progress=transfer_progress; w->sess.rc.http.progress_ctx=&progress;
        m->rc = rc_download(&w->sess.rc, m->a_url, &data);
        w->sess.rc.http.progress=NULL; w->sess.rc.http.progress_ctx=NULL;
        if(progress.serial && transfer_cancel[w->slot]==progress.serial) { m->rc=RC_ERR_ARGS; ar_strlcpy(m->text,"Download cancelled",sizeof(m->text)); }
        if (m->rc == RC_OK) {
            char part[600], backup[600];
            BPTR existing;
            int had_old = 0, staged = 0;
            snprintf(part, sizeof(part), "%s.ar-part-%08lx", m->a_text, (unsigned long)FindTask(NULL));
            snprintf(backup, sizeof(backup), "%s.ar-backup-%08lx", m->a_text, (unsigned long)FindTask(NULL));
            /* Never truncate the destination until a complete copy is on disk.
             * Refuse to overwrite leftovers from an interrupted transfer. */
            existing = Lock(part, ACCESS_READ);
            if (existing) { UnLock(existing); f = 0; }
            else f = Open(part, MODE_NEWFILE);
            if (!f) {
                m->rc = RC_ERR_ARGS;
                ar_strlcpy(m->text, "Cannot create a temporary download file", sizeof(m->text));
            } else {
                LONG written = Write(f, data.data, (LONG)data.len);
                LONG closed = Close(f);
                if (written == (LONG)data.len && closed) staged = 1;
                else DeleteFile(part);
                if (!staged) {
                    m->rc = RC_ERR_ARGS;
                    ar_strlcpy(m->text, "Could not write the complete file; original file kept", sizeof(m->text));
                }
            }
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
        } else if(!m->text[0]) ar_strlcpy(m->text, rc_error(&w->sess.rc), sizeof(m->text));
        buf_free(&data);
        break;
    }

    case NC_SEND:
        if (!w->have_sess || !w->sess.logged_in) {
            m->rc = RC_ERR_AUTH;
            ar_strlcpy(m->text, "Not connected", sizeof(m->text));
            break;
        }
        ar_strlcpy(m->rid, m->a_rid ? m->a_rid : "", sizeof(m->rid));
        m->rc = rc_send(&w->sess.rc, m->rid, m->a_text, &m->msgs);
        if (m->rc != RC_OK) {
            ar_strlcpy(m->text, rc_error(&w->sess.rc), sizeof(m->text));
            send_failure(w,m->rc,m->text);
        }
        break;

    case NC_POLL_NOW:
        do_poll(w);
        break;

    case NC_SET_FOCUS:
        w->foreground=m->a_interval!=0; w->sess.foreground=0;
        if(w->foreground && w->have_sess && w->sess.logged_in && m->a_rid && !strcmp(m->a_rid,w->sess.active_rid)) rc_mark_read(&w->sess.rc,w->sess.active_rid);
        break;

    case NC_SET_INTERVAL:
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
static void worker_entry(void)
{
    Worker *workers=ar_calloc(PREFS_SERVERS_MAX,sizeof(Worker));
    struct MsgPort *events=CreateMsgPort(); NetMsg *quit=NULL;
    ULONG cmdsig,evsig,mask=0; int i,ok=workers && events;
    g_cmdport=CreateMsgPort(); if(!g_cmdport) ok=0;
    for(i=0;ok && i<PREFS_SERVERS_MAX;i++) {
        Worker *w=&workers[i]; w->slot=i; w->interval=5; w->evreply=events;
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
    while(!quit) {
        ULONG sigs=Wait(mask); struct Message *msg;
        for(i=0;i<PREFS_SERVERS_MAX;i++) {
            Worker *w=&workers[i];
            if((sigs&(1UL<<w->tport->mp_SigBit)) && w->timer_pending && CheckIO((struct IORequest *)w->treq)) {
                WaitIO((struct IORequest *)w->treq); w->timer_pending=0;
                if(w->retry_login) {
                    int rc=sess_login(&w->sess,NULL,NULL,NULL,w->retry_uid,w->retry_token);
                    if(rc==RC_OK) {
                        NetMsg *e=netmsg_new(NE_RECONNECTED); w->retry_login=w->retry_delay=w->net_trouble=0;
                        if(e) { e->rc=RC_OK; fill_login_result(w,e); rc_roomlist_copy(&e->rooms,&w->sess.rooms); post_event(w,e); }
                    } else if(retryable_failure(w,rc)) schedule_retry(w,rc,rc_error(&w->sess.rc));
                    else { if(rc==RC_ERR_AUTH) send_failure(w,rc,rc_error(&w->sess.rc));
                           else { w->retry_login=0; w->sess.logged_in=0; post_status(w,rc,rc_error(&w->sess.rc)); } }
                } else do_poll(w);
                if(w->have_sess && (w->sess.logged_in || w->retry_login)) timer_start(w,w->retry_delay?w->retry_delay:w->interval);
            }
        }
        while((msg=GetMsg(events))) {
            NetMsg *m=(NetMsg *)msg;
            if(m->server_slot>=0 && m->server_slot<PREFS_SERVERS_MAX) workers[m->server_slot].outstanding--;
            netmsg_free(m);
        }
        while(!quit && (msg=GetMsg(g_cmdport))) {
            NetMsg *m=(NetMsg *)msg;
            if(m->kind==NC_QUIT) { quit=m; break; }
            if(m->kind==NC_SET_INTERVAL) { for(i=0;i<PREFS_SERVERS_MAX;i++) handle(&workers[i],m); }
            else if(m->server_slot>=0 && m->server_slot<PREFS_SERVERS_MAX) handle(&workers[m->server_slot],m);
            else m->rc=RC_ERR_ARGS;
            ReplyMsg(&m->msg);
        }
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

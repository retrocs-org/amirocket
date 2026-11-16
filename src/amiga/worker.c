#include "worker.h"
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
    return m;
}

void netmsg_free_payload(NetMsg *m)
{
    rc_roomlist_free(&m->rooms);
    rc_msglist_free(&m->msgs);
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

typedef struct {
    RcSession sess;
    int have_sess;
    int interval;
    int outstanding;           /* events not yet returned by the GUI */
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
    e->msg.mn_ReplyPort = w->evreply;
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

static void drop_session(Worker *w)
{
    timer_stop(w);
    if (w->have_sess)
        sess_cleanup(&w->sess);
    w->have_sess = 0;
    w->net_trouble = 0;
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
            w->sess.logged_in = 0;
            post_status(w, rc, t);
            return;
        }
        if (!w->net_trouble) {
            snprintf(t, sizeof(t), "Connection problem: %s - retrying", rc_error(&w->sess.rc));
            post_status(w, rc, t);
        }
        w->net_trouble = 1;
        return;
    }
    if (w->net_trouble) {
        w->net_trouble = 0;
        post_status(w, RC_OK, "Connection restored");
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
    switch (m->kind) {
    case NC_CONNECT:
        drop_session(w);
        m->rc = sess_init(&w->sess, net_amiga(), m->a_url ? m->a_url : "", m->a_verify);
        if (m->rc != RC_OK) {
            ar_strlcpy(m->text, rc_error(&w->sess.rc), sizeof(m->text));
            sess_cleanup(&w->sess);
            break;
        }
        w->have_sess = 1;
        m->rc = sess_login(&w->sess, m->a_user, m->a_pass, m->a_code, m->a_uid, m->a_token);
        fill_login_result(w, m);
        if (m->rc == RC_OK) {
            rc_roomlist_copy(&m->rooms, &w->sess.rooms);
            timer_start(w, w->interval);
        } else {
            ar_strlcpy(m->text, rc_error(&w->sess.rc), sizeof(m->text));
            if (!w->sess.logged_in)
                drop_session(w);
        }
        break;

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

    case NC_SEND:
        if (!w->have_sess || !w->sess.logged_in) {
            m->rc = RC_ERR_AUTH;
            ar_strlcpy(m->text, "Not connected", sizeof(m->text));
            break;
        }
        ar_strlcpy(m->rid, m->a_rid ? m->a_rid : "", sizeof(m->rid));
        m->rc = rc_send(&w->sess.rc, m->rid, m->a_text, &m->msgs);
        if (m->rc != RC_OK)
            ar_strlcpy(m->text, rc_error(&w->sess.rc), sizeof(m->text));
        break;

    case NC_POLL_NOW:
        do_poll(w);
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
        break;
    }
    m->server_now = w->have_sess ? sess_server_now(&w->sess) : 0;
}

static void worker_entry(void)
{
    Worker w;
    NetMsg *quit = NULL;
    ULONG cmdsig, evsig, tsig;

    memset(&w, 0, sizeof(w));
    w.interval = 5;
    g_cmdport = CreateMsgPort();
    w.evreply = CreateMsgPort();
    w.tport = CreateMsgPort();
    if (w.tport)
        w.treq = (struct timerequest *)CreateIORequest(w.tport, sizeof(struct timerequest));
    if (w.treq && OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)w.treq, 0) == 0)
        w.timer_open = 1;
    if (!g_cmdport || !w.evreply || !w.timer_open) {
        ar_strlcpy(g_starterr, "Cannot set up the network process", sizeof(g_starterr));
        if (w.timer_open)
            CloseDevice((struct IORequest *)w.treq);
        if (w.treq)
            DeleteIORequest((struct IORequest *)w.treq);
        if (w.tport)
            DeleteMsgPort(w.tport);
        if (w.evreply)
            DeleteMsgPort(w.evreply);
        if (g_cmdport)
            DeleteMsgPort(g_cmdport);
        g_cmdport = NULL;
        Forbid();
        g_started = -1;
        Signal(g_parent, 1UL << g_startsig);
        return;
    }
    g_started = 1;
    Signal(g_parent, 1UL << g_startsig);

    cmdsig = 1UL << g_cmdport->mp_SigBit;
    evsig = 1UL << w.evreply->mp_SigBit;
    tsig = 1UL << w.tport->mp_SigBit;

    while (!quit) {
        ULONG sigs = Wait(cmdsig | evsig | tsig | SIGBREAKF_CTRL_C);
        struct Message *msg;

        if ((sigs & tsig) && w.timer_pending && CheckIO((struct IORequest *)w.treq)) {
            WaitIO((struct IORequest *)w.treq);
            w.timer_pending = 0;
            do_poll(&w);
            if (w.have_sess && w.sess.logged_in)
                timer_start(&w, w.interval);
        }
        while ((msg = GetMsg(w.evreply)) != NULL) {
            netmsg_free((NetMsg *)msg);
            w.outstanding--;
        }
        while (!quit && (msg = GetMsg(g_cmdport)) != NULL) {
            NetMsg *m = (NetMsg *)msg;
            if (m->kind == NC_QUIT) {
                quit = m;
                break;
            }
            handle(&w, m);
            ReplyMsg(&m->msg);
            /* drain returned events between commands as well */
            while ((msg = GetMsg(w.evreply)) != NULL) {
                netmsg_free((NetMsg *)msg);
                w.outstanding--;
            }
        }
    }

    /* Shutdown: wait for every event to come back before deleting ports. */
    drop_session(&w);
    net_amiga_close();
    while (w.outstanding > 0) {
        struct Message *msg;
        WaitPort(w.evreply);
        while ((msg = GetMsg(w.evreply)) != NULL) {
            netmsg_free((NetMsg *)msg);
            w.outstanding--;
        }
    }
    {
        struct Message *msg;
        while ((msg = GetMsg(g_cmdport)) != NULL) {   /* refuse late commands */
            ((NetMsg *)msg)->rc = RC_ERR_NET;
            ReplyMsg(msg);
        }
    }
    CloseDevice((struct IORequest *)w.treq);
    DeleteIORequest((struct IORequest *)w.treq);
    DeleteMsgPort(w.tport);
    DeleteMsgPort(w.evreply);
    Forbid();
    DeleteMsgPort(g_cmdport);
    g_cmdport = NULL;
    ReplyMsg(&quit->msg);
    /* still in Forbid(): the process ends before the GUI can unload us */
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

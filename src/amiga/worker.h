/*
 * AmiRocket - network worker process.
 *
 * All networking runs in a separate process so the GUI never blocks on a
 * TLS handshake or a slow server. The GUI sends NetMsg commands to the
 * worker; the worker replies with the same message filled in as a result
 * (NT_REPLYMSG). Poll results arrive as worker-allocated event messages
 * (NT_MESSAGE) that the GUI must hand back with worker_done().
 *
 * Payload lists inside a received message belong to the GUI: take what you
 * need and free the rest with netmsg_free_payload() (worker_done() and
 * netmsg_free() do this too).
 */
#ifndef AR_WORKER_H
#define AR_WORKER_H

#include <exec/ports.h>
#include "../core/rc_api.h"

enum {
    /* commands (GUI -> worker) */
    NC_CONNECT = 1,     /* a_url, a_user/a_pass/a_code or a_uid/a_token, a_verify */
    NC_CREATE_ROOM,     /* a_user: type c/p/d; a_text: name, a_pass: members CSV */
    NC_IMAGE,           /* a_url: same-origin image; avatar: downloaded bytes */
    NC_OPEN_ROOM,       /* a_rid */
    NC_AVATAR,          /* a_user; avatar bytes, cosmetic only */
    NC_PRESENCE,        /* members: username\tstatus lines */
    NC_HISTORY,         /* a_rid, a_before: older page; does not alter sync cursor */
    NC_DOWNLOAD,        /* a_url, a_text: attachment URL and chosen local path */
    NC_SEND,            /* a_rid, a_text (UTF-8) */
    NC_POLL_NOW,
    NC_SET_FOCUS,       /* a_interval: selected server (1), background (0) */
    NC_SET_INTERVAL,    /* a_interval seconds */
    NC_LOGOUT,          /* revoke the token on the server */
    NC_DISCONNECT,      /* just drop the session */
    NC_QUIT,

    /* events (worker -> GUI) */
    NE_UPDATE = 100,    /* rooms: delta, msgs: delta for rid */
    NE_PRESENCE,
    NE_RECONNECTED,     /* successful automatic login, same payload as NC_CONNECT */
    NE_TRANSFER,        /* a_before: bytes received, a_interval: total bytes */
    NE_STATUS           /* text: connection state change, rc: last error */
};

typedef struct NetMsg {
    struct Message msg;
    int   server_slot;          /* stable configuration slot, 0..7 */
    int   server_generation;    /* rejects late replies after profile replacement */
    int   kind;
    int   rc;                   /* RC_OK or RC_ERR_* */
    char  text[256];            /* error or status text */

    /* command arguments (owned by the message, ar_malloc'd) */
    char *a_url, *a_user, *a_pass, *a_code, *a_uid, *a_token, *a_rid, *a_text;
    ar_ms a_before;
    int   a_verify;
    int   a_interval;

    /* results */
    char  rid[64];
    RcRoomList rooms;
    RcMsgList  msgs;
    Buf avatar;
    char  username[96];
    char  display_name[96];
    char  user_id[64];
    char  token[128];
    char  server_version[32];
    char  totp_method[16];
    char  topic[256];           /* NC_OPEN_ROOM: room topic, UTF-8 */
    char *members;              /* NC_OPEN_ROOM: "username\tname\tstatus\n" lines */
    ar_ms server_now;           /* server clock from the last HTTP Date header */
    ar_ms sent_at;              /* GUI-side bookkeeping */
} NetMsg;

/* Starts the worker; returns its command port's signal holder, or -1. */
int  worker_start(struct MsgPort *gui_port, char *err, int errsz);
void worker_cancel_transfer(int slot,ar_ms serial);
void worker_stop(void);         /* NC_QUIT + wait; safe to call twice */

NetMsg *netmsg_new(int kind);
void    netmsg_free_payload(NetMsg *m);
void    netmsg_free(NetMsg *m);
void    worker_send(NetMsg *m); /* PutMsg to the worker */
void    worker_done(NetMsg *m); /* return an NT_MESSAGE event */

#endif

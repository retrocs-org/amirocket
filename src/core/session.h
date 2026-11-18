/*
 * AmiRocket - session: login state, room list and the polling strategy.
 *
 * Polling (REST only, no WebSocket yet):
 *   rooms:       subscriptions.get?updatedSince=<newest subscription change>
 *   open room:   chat.syncMessages?lastUpdate=<newest message change>
 * Timestamps always come from the server (message _updatedAt or the HTTP
 * Date header), never from the Amiga clock, so clock skew does not matter.
 */
#ifndef AR_SESSION_H
#define AR_SESSION_H

#include "rc_api.h"

typedef struct {
    RcClient   rc;
    RcRoomList rooms;
    int        logged_in;
    int        foreground; /* background polling must not mark messages as read */

    char  active_rid[64];
    char  active_type;
    ar_ms active_last;      /* sync cursor for the open room */
    char  active_topic[256];/* topic of the open room (UTF-8), "" if none */
    char *active_members;   /* "username\tname\tstatus\n" lines, or NULL */
    int   history_count;    /* messages fetched when a room is opened */
} RcSession;

int  sess_init(RcSession *s, const NetIface *net, const char *url, int verify_tls);
void sess_cleanup(RcSession *s);

/* Login, then fetch the initial room list into s->rooms. Pass either
 * user+password(+code) or user_id+token. */
int sess_login(RcSession *s, const char *user, const char *password, const char *code,
               const char *user_id, const char *token);

/* Opens a room: loads history (oldest first) into out and marks it read. */
int sess_open_room(RcSession *s, const char *rid, RcMsgList *out);

/* One poll cycle. Room changes replace the contents of room_delta (and are
 * merged into s->rooms as well; removed rids are in room_delta->removed) and new/changed/deleted messages of the open room to
 * msg_delta. Either pointer may be NULL to skip that part. */
int sess_poll(RcSession *s, RcRoomList *room_delta, RcMsgList *msg_delta);

const RcRoom *sess_find_room(const RcSession *s, const char *rid_or_name);

/* Estimated offset local-clock minus server-clock is the caller's business;
 * this returns the server's idea of "now" from the last response (0 if
 * unknown). */
ar_ms sess_server_now(const RcSession *s);

#endif

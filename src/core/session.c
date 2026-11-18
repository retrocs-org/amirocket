#include "session.h"
#include "mem.h"
#include "util.h"

#include <string.h>

int sess_init(RcSession *s, const NetIface *net, const char *url, int verify_tls)
{
    int rc;

    memset(s, 0, sizeof(*s));
    rc_roomlist_init(&s->rooms);
    s->history_count = 50; s->foreground=1;
    rc = rc_init(&s->rc, net, url, verify_tls);
    return rc;
}

void sess_cleanup(RcSession *s)
{
    ar_free(s->active_members);
    s->active_members = NULL;
    rc_roomlist_free(&s->rooms);
    rc_cleanup(&s->rc);
    s->logged_in = 0;
    s->active_rid[0] = 0;
}

int sess_login(RcSession *s, const char *user, const char *password, const char *code,
               const char *user_id, const char *token)
{
    RcRoomList fresh;
    int rc;

    s->logged_in = 0;
    rc_server_info(&s->rc);   /* best effort; version may be hidden */
    if (token && *token)
        rc = rc_login_token(&s->rc, user_id, token);
    else
        rc = rc_login_password(&s->rc, user, password, code);
    if (rc != RC_OK)
        return rc;
    s->logged_in = 1;

    rc_roomlist_free(&s->rooms);
    rc_roomlist_init(&fresh);
    rc = rc_rooms(&s->rc, 0, &fresh);
    if (rc == RC_OK)
        rc_roomlist_merge(&s->rooms, &fresh);
    rc_roomlist_free(&fresh);
    return rc;
}

const RcRoom *sess_find_room(const RcSession *s, const char *key)
{
    int i;

    if (!key)
        return NULL;
    if (*key == '#' || *key == '@')
        key++;
    for (i = 0; i < s->rooms.count; i++)
        if (!strcmp(s->rooms.items[i].rid, key))
            return &s->rooms.items[i];
    for (i = 0; i < s->rooms.count; i++)
        if (!ar_strcasecmp(s->rooms.items[i].name, key))
            return &s->rooms.items[i];
    for (i = 0; i < s->rooms.count; i++)
        if (s->rooms.items[i].fname && !ar_strcasecmp(s->rooms.items[i].fname, key))
            return &s->rooms.items[i];
    return NULL;
}

int sess_open_room(RcSession *s, const char *rid, RcMsgList *out)
{
    const RcRoom *r = sess_find_room(s, rid);
    RcMsgList hist;
    int rc;

    if (!r)
        return RC_ERR_ARGS;
    rc_msglist_init(&hist);
    rc = rc_history(&s->rc, r->rid, r->type, s->history_count, 0, &hist);
    if (rc != RC_OK) {
        rc_msglist_free(&hist);
        return rc;
    }
    ar_strlcpy(s->active_rid, r->rid, sizeof(s->active_rid));
    s->active_type = r->type;
    /* Cursor: newest change we have seen, or the server's clock for an
     * empty room so that sync picks up anything that arrives later. */
    s->active_last = hist.newest_ms ? hist.newest_ms : s->rc.http.last_date_ms;
    rc_msglist_merge(out, &hist, 0);
    rc_msglist_free(&hist);
    if (s->foreground && (r->unread || r->alert))
        rc_mark_read(&s->rc, s->active_rid);   /* best effort */
    if (r->type != 'd')
        rc_room_topic(&s->rc, s->active_rid, s->active_topic, sizeof(s->active_topic));
    else
        s->active_topic[0] = 0;
    ar_free(s->active_members);
    s->active_members = NULL;
    rc_room_members(&s->rc, s->active_rid, s->active_type, 200, &s->active_members);  /* best effort */
    return RC_OK;
}

int sess_poll(RcSession *s, RcRoomList *room_delta, RcMsgList *msg_delta)
{
    int rc = RC_OK;

    if (!s->logged_in)
        return RC_ERR_AUTH;

    if (msg_delta && s->active_rid[0]) {
        RcMsgList got;
        ar_ms before = s->active_last;
        rc_msglist_init(&got);
        if (s->active_last > 0) {
            rc = rc_sync(&s->rc, s->active_rid, s->active_last, &got);
        } else {
            rc = rc_history(&s->rc, s->active_rid, s->active_type, s->history_count, 0, &got);
            if (rc == RC_OK && !got.newest_ms)
                got.newest_ms = s->rc.http.last_date_ms;
        }
        if (rc == RC_OK) {
            if (got.newest_ms > s->active_last)
                s->active_last = got.newest_ms;
            /* the cursor is inclusive on some server versions; only a
             * genuinely newer change marks the room as read */
            if (s->foreground && got.newest_ms > before && got.count > 0)
                rc_mark_read(&s->rc, s->active_rid);
            /* hand deletions to the caller too (merge would consume them) */
            if (got.n_deleted) {
                char **na = (char **)ar_realloc(msg_delta->deleted,
                    sizeof(char *) * (size_t)(msg_delta->n_deleted + got.n_deleted));
                if (na) {
                    int i;
                    msg_delta->deleted = na;
                    for (i = 0; i < got.n_deleted; i++)
                        msg_delta->deleted[msg_delta->n_deleted++] = got.deleted[i];
                    ar_free(got.deleted);
                    got.deleted = NULL;
                    got.n_deleted = 0;
                }
            }
            rc_msglist_merge(msg_delta, &got, 0);
        }
        rc_msglist_free(&got);
        if (rc != RC_OK)
            return rc;
    }

    if (room_delta) {
        RcRoomList got, copy;
        rc_roomlist_init(&got);
        rc = rc_rooms(&s->rc, s->rooms.newest_ms, &got);
        if (rc == RC_OK && (got.count || got.n_removed)) {
            /* hand a copy to the caller, merge the original into our state */
            if (rc_roomlist_copy(&copy, &got) == 0) {
                rc_roomlist_free(room_delta);   /* replaced, keeps removed ids */
                *room_delta = copy;
            }
            rc_roomlist_merge(&s->rooms, &got);
        }
        rc_roomlist_free(&got);
    }
    return rc;
}

ar_ms sess_server_now(const RcSession *s)
{
    return s->rc.http.last_date_ms;
}

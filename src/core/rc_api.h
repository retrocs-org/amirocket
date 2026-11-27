/*
 * AmiRocket - Rocket.Chat REST API client.
 *
 * Synchronous calls over one persistent HTTP connection. All strings in the
 * data model are UTF-8 exactly as the server sent them; conversion to the
 * Amiga's ISO-8859-1 happens in the user interface.
 *
 * Endpoints used (all under /api/v1 unless noted):
 *   GET  /api/info                 server version (public)
 *   POST login                     user+password(+code), or resume token
 *   GET  me                        validate a stored token
 *   POST logout
 *   GET  subscriptions.get         room list, incremental with updatedSince
 *   GET  channels|groups|im.history  initial room history
 *   GET  chat.syncMessages         incremental updates for the open room
 *   POST chat.sendMessage
 *   POST subscriptions.read
 */
#ifndef AR_RC_API_H
#define AR_RC_API_H

#include "http.h"
#include "url.h"
#include "timeutil.h"

enum {
    RC_OK = 0,
    RC_ERR_NET = -1,        /* connection / TLS / timeout */
    RC_ERR_HTTP = -2,       /* unexpected HTTP status without usable body */
    RC_ERR_AUTH = -3,       /* bad credentials or expired token */
    RC_ERR_TOTP = -4,       /* two-factor code required */
    RC_ERR_TOTP_INVALID = -5,
    RC_ERR_PROTO = -6,      /* malformed JSON / unexpected shape */
    RC_ERR_SERVER = -7,     /* server reported success:false */
    RC_ERR_NOMEM = -8,
    RC_ERR_ARGS = -9
};

typedef struct {
    char *rid;
    char *name;        /* room name, or the other user's username for DMs */
    char *fname;       /* display name, may be NULL */
    char  type;        /* 'c' public, 'p' private, 'd' direct, 'l' livechat */
    int   unread;
    int   alert;
    int   mentions;   /* userMentions + groupMentions */
    ar_ms last_seen_ms; /* subscription ls, captured before marking read */
    int   open;
    int   member_count; /* authoritative group size when supplied by server */
    int   favorite;    /* starred by the user ("f") */
    ar_ms updated_ms;
} RcRoom;

typedef struct {
    RcRoom *items;
    int     count, cap;
    char  **removed;      /* rids removed since the last update */
    int     n_removed;
    ar_ms   newest_ms;    /* max updated_ms, feed back as updatedSince */
} RcRoomList;

typedef struct {
    char *title, *url;
    char *preview_url; /* server image_url; url remains the original download */
    int image;
    long long size;   /* encoded bytes reported by server, 0 = unknown */
} RcAttachment;

typedef struct {
    char *id;
    char *rid;
    char *username;
    char *name;           /* display name, may be NULL */
    char *text;
    char *type;           /* system message type ("uj", "ul", ...) or NULL */
    char *tmid;           /* thread parent id, or NULL */
    char *attachment;     /* first attachment title / file name, or NULL */
    char *reaction_users; /* bounded shortcode TAB username rows for toggling own badge */
    char *reactions;      /* "heart_eyes 1\n+1 2" (shortcode count per line), or NULL */
    RcAttachment *files;
    int   file_count;
    int   n_attachments;
    int   edited;
    ar_ms ts_ms;
    ar_ms updated_ms;
} RcMsg;

typedef struct {
    RcMsg *items;
    int    count, cap;
    char **deleted;       /* ids of deleted messages (sync only) */
    int    n_deleted;
    ar_ms  newest_ms;     /* max updated_ms seen */
} RcMsgList;

typedef struct {
    HttpClient http;
    ServerUrl  url;
    char user_id[64];
    char token[128];
    char username[96];
    char display_name[96];
    char server_version[32];
    char totp_method[16];   /* "totp", "email" or "password" when RC_ERR_TOTP */
    char err[256];
    int  last_status;
} RcClient;

int  rc_init(RcClient *c, const NetIface *net, const char *server_url, int verify_tls);
void rc_cleanup(RcClient *c);
const char *rc_error(const RcClient *c);
const char *rc_strerror(int code);

int rc_server_info(RcClient *c);
int rc_login_password(RcClient *c, const char *user, const char *password, const char *code);
int rc_login_token(RcClient *c, const char *user_id, const char *token);
int rc_logout(RcClient *c);

int rc_rooms(RcClient *c, ar_ms updated_since, RcRoomList *out);
int rc_history(RcClient *c, const char *rid, char type, int count, ar_ms before, RcMsgList *out);
int rc_sync(RcClient *c, const char *rid, ar_ms last_update, RcMsgList *out);
int rc_send_id(RcClient *c,const char *rid,const char *text,const char *id,RcMsgList *echo);
int rc_confirm_send(RcClient *c,const char *rid,const char *text,const char *id,RcMsgList *echo);
int rc_send(RcClient *c, const char *rid, const char *text, RcMsgList *echo);
/* Create public/private channels or open a single/group DM. Members are comma-separated usernames. */
int rc_send_thread(RcClient *c,const char *rid,const char *tmid,const char *text,RcMsgList *echo);
int rc_thread(RcClient *c,const char *tmid,int offset,RcMsgList *out,int *total);
int rc_message_edit(RcClient *c,const char *rid,const char *id,const char *text,RcMsgList *echo);
int rc_message_delete(RcClient *c,const char *rid,const char *id);
int rc_reaction_has(const RcMsg *message,const char *emoji,const char *username);
int rc_message_react(RcClient *c,const char *id,const char *emoji,int add);
int rc_create_room(RcClient *c, char type, const char *name, const char *members, RcRoomList *out);
int rc_search(RcClient *c,const char *rid,const char *query,int offset,int count,RcMsgList *out,int *total);
/* Stream one temporary upload, then explicitly confirm it. No automatic replay. */
int rc_upload(RcClient *c,const char *rid,const char *filename,size_t length,
              int (*read_file)(void *,void *,int),void *ctx,char file_id[96]);
int rc_upload_confirm(RcClient *c,const char *rid,const char *file_id);
int rc_mark_read(RcClient *c, const char *rid);
int rc_room_topic(RcClient *c, const char *rid, char *out, size_t outsz);  /* "" if none */
/* Room members as lines "username\tname\tstatus" (UTF-8, ar_malloc'd). */
int rc_room_members(RcClient *c, const char *rid, char type, int count, char **out);

/* Download authenticated attachments; max 8 MiB. HTTPS storage redirects omit credentials. */
int rc_download(RcClient *c, const char *url, Buf *data);
int rc_download_to(RcClient *c,const char *url,int (*write)(void *,const void *,size_t),void *ctx);

/* Cosmetic metadata; failures must not interrupt chat. Presence is username\tstatus\n. */
int rc_presence(RcClient *c, char **out);
int rc_avatar(RcClient *c, const char *username, Buf *data); /* max 256 KiB */
const char *rc_attachment_preview_url(const RcAttachment *file);
int rc_dm_participants(const RcRoom *room, const char *me);

/* Data model helpers */
void rc_room_free(RcRoom *r);
void rc_roomlist_init(RcRoomList *l);
void rc_roomlist_free(RcRoomList *l);
const char *rc_room_title(const RcRoom *r);       /* fname or name */

void rc_msg_free(RcMsg *m);
void rc_msglist_init(RcMsgList *l);
void rc_msglist_free(RcMsgList *l);

/* Merge src into dst: upsert by id, apply deletions, keep sorted by ts and
 * trim to at most max_keep newest messages (0 = unlimited). src is emptied
 * (ownership of its strings moves into dst). */
int rc_msglist_merge(RcMsgList *dst, RcMsgList *src, int max_keep);
/* Same for rooms (upsert by rid, apply removed). */
int rc_roomlist_merge(RcRoomList *dst, RcRoomList *src);
void rc_roomlist_sort(RcRoomList *l);   /* channels/groups, then DMs; by name */
int  rc_room_compare(const RcRoom *a, const RcRoom *b);  /* by name, ignoring leading punctuation */
/* Deep copy (items and removed ids). Returns 0, or -1 when out of memory. */
int rc_roomlist_copy(RcRoomList *dst, const RcRoomList *src);

/* Human readable text for system messages ("joined the room" ...).
 * Returns NULL for ordinary messages. */
const char *rc_system_text(const char *type);

#endif

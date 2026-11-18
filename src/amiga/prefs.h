/*
 * AmiRocket - settings in ENVARC:AmiRocket.prefs (legacy ENV: read fallback) (plain key=value).
 *
 * With "remember" on, the Rocket.Chat user id and auth token are stored so
 * the next start logs in without a password. The password itself is never
 * stored. The token is a credential: anyone who can read ENVARC: can use
 * the account until the token is revoked (Project > Log out).
 */
#ifndef AR_PREFS_H
#define AR_PREFS_H

#define PREFS_SERVERS_MAX 8
/* Fixed slots remain stable when another server is removed. No passwords. */
typedef struct {
    char url[256], name[96], user[96], user_id[64], token[128];
    int verify, remember, autoconnect;
} ServerPrefs;
#define PREFS_EXTRA_MAX 48
typedef struct { char key[64], value[256]; } PrefsKey;
typedef struct {
    char server[256];
    char user[96];
    char user_id[64];
    char token[128];
    int  verify;        /* verify TLS certificates */
    int  remember;
    int  interval;      /* poll interval in seconds */
    char theme[24];     /* theme id, see theme.c */
    int  ownscreen;     /* themes on their own screen instead of a Workbench window */
    char chat_font[128];
    int  chat_font_size;
    int  preview_images;
    int  show_emoji, show_avatars, inline_images, open_links;
    char browser[128], image_viewer[128];
    int  win_left, win_top, win_width, win_height;
    ServerPrefs servers[PREFS_SERVERS_MAX];
    int servers_defined; /* distinguishes an empty list from legacy settings */
    int extra_count;
    PrefsKey extra[PREFS_EXTRA_MAX];
} Prefs;

void prefs_defaults(Prefs *p);
void prefs_migrate_servers(Prefs *p);
int prefs_server_validate(const ServerPrefs *server, char *error, int size);
int prefs_server_equal(const ServerPrefs *a, const ServerPrefs *b);
int prefs_save_server(int slot,const ServerPrefs *server);
int prefs_lock(void);
void prefs_unlock(void);
int  prefs_load(Prefs *p);    /* 0 success, -1 missing/unreadable, -2 unsupported size/key capacity */
int  prefs_save(const Prefs *p); /* single persistent file: ENVARC:AmiRocket.prefs */
int prefs_set(Prefs *p, const char *key, const char *value); /* validates editable UI/plugin keys */
int prefs_get(const Prefs *p, const char *key, char *out, int size);
const char *prefs_extra(const Prefs *p, const char *key, const char *fallback);

#endif

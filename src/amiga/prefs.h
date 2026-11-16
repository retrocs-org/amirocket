/*
 * AmiRocket - settings in ENV:/ENVARC:AmiRocket.prefs (plain key=value).
 *
 * With "remember" on, the Rocket.Chat user id and auth token are stored so
 * the next start logs in without a password. The password itself is never
 * stored. The token is a credential: anyone who can read ENVARC: can use
 * the account until the token is revoked (Project > Log out).
 */
#ifndef AR_PREFS_H
#define AR_PREFS_H

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
    int  win_left, win_top, win_width, win_height;
} Prefs;

void prefs_defaults(Prefs *p);
int  prefs_load(Prefs *p);    /* 0 if a file was read */
int  prefs_save(const Prefs *p);

#endif

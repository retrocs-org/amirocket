#include "prefs.h"
#include "../core/buf.h"
#include "../core/util.h"

#include <dos/dos.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREFS_NAME "AmiRocket.prefs"

void prefs_defaults(Prefs *p)
{
    memset(p, 0, sizeof(*p));
    p->verify = 1;
    p->remember = 1;
    p->interval = 5;
    ar_strlcpy(p->theme, "workbench", sizeof(p->theme));
    p->win_left = -1;
    p->win_top = -1;
    p->win_width = 640;
    p->win_height = 400;
}

static void set_kv(Prefs *p, const char *k, const char *v)
{
    if (!strcmp(k, "server")) ar_strlcpy(p->server, v, sizeof(p->server));
    else if (!strcmp(k, "user")) ar_strlcpy(p->user, v, sizeof(p->user));
    else if (!strcmp(k, "user_id")) ar_strlcpy(p->user_id, v, sizeof(p->user_id));
    else if (!strcmp(k, "token")) ar_strlcpy(p->token, v, sizeof(p->token));
    else if (!strcmp(k, "verify")) p->verify = atoi(v) != 0;
    else if (!strcmp(k, "remember")) p->remember = atoi(v) != 0;
    else if (!strcmp(k, "interval")) p->interval = atoi(v);
    else if (!strcmp(k, "theme")) ar_strlcpy(p->theme, v, sizeof(p->theme));
    else if (!strcmp(k, "ownscreen")) p->ownscreen = atoi(v) != 0;
    else if (!strcmp(k, "window")) {
        sscanf(v, "%d,%d,%d,%d", &p->win_left, &p->win_top, &p->win_width, &p->win_height);
    }
}

int prefs_load(Prefs *p)
{
    static const char *paths[] = { "ENV:" PREFS_NAME, "ENVARC:" PREFS_NAME, NULL };
    char data[2048];
    BPTR fh = 0;
    LONG n;
    int i;
    char *line, *next;

    prefs_defaults(p);
    for (i = 0; paths[i] && !fh; i++)
        fh = Open((CONST_STRPTR)paths[i], MODE_OLDFILE);
    if (!fh)
        return -1;
    n = Read(fh, data, sizeof(data) - 1);
    Close(fh);
    if (n <= 0)
        return -1;
    data[n] = 0;
    for (line = data; line && *line; line = next) {
        char *eq;
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        ar_trim(line);
        if (!*line || *line == '#' || !(eq = strchr(line, '=')))
            continue;
        *eq = 0;
        set_kv(p, ar_trim(line), ar_trim(eq + 1));
    }
    if (p->interval < 2 || p->interval > 600)
        p->interval = 5;
    return 0;
}

static int write_file(const char *path, const Buf *b)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    LONG w;

    if (!fh)
        return -1;
    w = Write(fh, (APTR)buf_str(b), (LONG)b->len);
    Close(fh);
    return w == (LONG)b->len ? 0 : -1;
}

int prefs_save(const Prefs *p)
{
    Buf b;
    int rc;

    buf_init(&b);
    buf_puts(&b, "# AmiRocket settings\n");
    buf_printf(&b, "server=%s\nuser=%s\nverify=%d\nremember=%d\ninterval=%d\n",
               p->server, p->user, p->verify, p->remember, p->interval);
    buf_printf(&b, "window=%d,%d,%d,%d\n", p->win_left, p->win_top, p->win_width, p->win_height);
    buf_printf(&b, "theme=%s\nownscreen=%d\n", p->theme, p->ownscreen);
    if (p->remember && p->token[0])
        buf_printf(&b, "user_id=%s\ntoken=%s\n", p->user_id, p->token);
    rc = write_file("ENV:" PREFS_NAME, &b);
    if (write_file("ENVARC:" PREFS_NAME, &b) < 0)
        rc = -1;
    memset(b.data, 0, b.len);
    buf_free(&b);
    return rc;
}

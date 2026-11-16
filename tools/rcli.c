/*
 * rcli - command line Rocket.Chat client built from the AmiRocket core.
 *
 * Exercises exactly the code the Amiga client uses (HTTP, JSON, API,
 * session, charset), so it doubles as an integration test tool and as a
 * way to check a real server from a Mac or Linux box.
 *
 *   rcli -s https://chat.example.com -u tommy -p secret rooms
 *   RC_USER_ID=... RC_TOKEN=... rcli -s URL history general 20
 */
#include "../src/core/session.h"
#include "../src/core/charset.h"
#include "../src/core/mdtext.h"
#include "../src/core/mem.h"
#include "../src/posix/net_posix.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_latin1;    /* -L: print through the Amiga conversion path */

static void usage(void)
{
    fprintf(stderr,
        "usage: rcli [options] command [args]\n"
        "  -s URL      server (or RC_URL)\n"
        "  -u USER     user name or e-mail (or RC_USER)\n"
        "  -p PASS     password (or RC_PASSWORD)\n"
        "  -c CODE     two-factor code\n"
        "  -i ID -t T  user id and token / personal access token (or RC_USER_ID, RC_TOKEN)\n"
        "  -k          do not verify the TLS certificate\n"
        "  -L          print text as the Amiga client shows it (markup removed, ISO-8859-1)\n"
        "commands:\n"
        "  info                     server version (no login)\n"
        "  login                    log in and print user id and token\n"
        "  rooms                    list subscriptions\n"
        "  history ROOM [N]         last N messages (default 20)\n"
        "  send ROOM TEXT...        send a message\n"
        "  watch ROOM [SECS] [N]    poll every SECS seconds (default 5), N cycles (0=forever)\n");
}

static void put_text(const char *utf8)
{
    if (g_latin1) {   /* exactly what the Amiga GUI shows */
        Buf b, md;
        buf_init(&b);
        buf_init(&md);
        md_to_plain(&md, utf8);
        utf8_to_latin1(&b, buf_str(&md), 1);
        fputs(buf_str(&b), stdout);
        buf_free(&md);
        buf_free(&b);
    } else {
        fputs(utf8, stdout);
    }
}

static void print_msg(const RcMsg *m)
{
    char iso[32];
    const char *sys = rc_system_text(m->type);

    ms_to_iso8601(m->ts_ms, iso);
    printf("%s <%s> ", iso, m->username);
    if (sys) {
        printf("* %s ", sys);
    }
    put_text(m->text);
    if (m->n_attachments)
        printf(" [attachment: %s]", m->attachment ? m->attachment : "file");
    if (m->edited)
        printf(" (edited)");
    if (m->tmid)
        printf(" (thread)");
    if (m->reactions) {
        const char *p;
        printf(" [reactions:");
        for (p = m->reactions; *p; p++)
            putchar(*p == '\n' ? ',' : *p);
        printf("]");
    }
    printf("\n");
}

static void print_room(const RcRoom *r)
{
    printf("%c %-24s %-20s unread=%d%s%s%s rid=%s\n", r->type, r->name, r->fname ? r->fname : "",
           r->unread, r->alert ? " alert" : "", r->favorite ? " fav" : "", r->open ? "" : " hidden", r->rid);
}

static int fail(RcSession *s, int rc)
{
    fprintf(stderr, "rcli: %s: %s\n", rc_strerror(rc), rc_error(&s->rc));
    return rc == RC_ERR_TOTP ? 3 : rc == RC_ERR_AUTH ? 2 : 1;
}

int main(int argc, char **argv)
{
    const char *url = getenv("RC_URL"), *user = getenv("RC_USER"), *pass = getenv("RC_PASSWORD");
    const char *uid = getenv("RC_USER_ID"), *token = getenv("RC_TOKEN"), *code = NULL;
    const char *cmd;
    int verify = 1, opt, rc, ret = 0;
    RcSession s;

    signal(SIGPIPE, SIG_IGN);
    while ((opt = getopt(argc, argv, "s:u:p:c:i:t:kLh")) != -1) {
        switch (opt) {
        case 's': url = optarg; break;
        case 'u': user = optarg; break;
        case 'p': pass = optarg; break;
        case 'c': code = optarg; break;
        case 'i': uid = optarg; break;
        case 't': token = optarg; break;
        case 'k': verify = 0; break;
        case 'L': g_latin1 = 1; break;
        default: usage(); return 64;
        }
    }
    if (optind >= argc || !url) {
        usage();
        return 64;
    }
    cmd = argv[optind++];
    ar_mem_init();
    if ((rc = sess_init(&s, net_posix(), url, verify)) != RC_OK) {
        fprintf(stderr, "rcli: %s\n", rc_error(&s.rc));
        return 64;
    }

    if (!strcmp(cmd, "info")) {
        rc = rc_server_info(&s.rc);
        if (rc != RC_OK)
            ret = fail(&s, rc);
        else
            printf("version=%s\n", s.rc.server_version[0] ? s.rc.server_version : "(hidden)");
        goto done;
    }

    rc = sess_login(&s, user, pass, code, uid, token);
    if (rc != RC_OK) {
        ret = fail(&s, rc);
        goto done;
    }

    if (!strcmp(cmd, "login")) {
        printf("user=%s\nname=%s\nuser_id=%s\ntoken=%s\nrooms=%d\nversion=%s\n",
               s.rc.username, s.rc.display_name, s.rc.user_id, s.rc.token, s.rooms.count,
               s.rc.server_version);
    } else if (!strcmp(cmd, "rooms")) {
        int i;
        for (i = 0; i < s.rooms.count; i++)
            print_room(&s.rooms.items[i]);
    } else if (!strcmp(cmd, "history") || !strcmp(cmd, "watch")) {
        RcMsgList msgs;
        int i, watch = !strcmp(cmd, "watch");
        if (optind >= argc) {
            usage();
            ret = 64;
            goto done;
        }
        if (!watch && optind + 1 < argc)
            s.history_count = atoi(argv[optind + 1]);
        else if (!watch)
            s.history_count = 20;
        rc_msglist_init(&msgs);
        rc = sess_open_room(&s, argv[optind], &msgs);
        if (rc == RC_ERR_ARGS) {
            fprintf(stderr, "rcli: no such room: %s\n", argv[optind]);
            ret = 1;
            goto done;
        }
        if (rc != RC_OK) {
            ret = fail(&s, rc);
            goto done;
        }
        if (s.active_topic[0])
            printf("topic: %s\n", s.active_topic);
        if (s.active_members && getenv("RCLI_MEMBERS")) {
            char *p;
            for (p = s.active_members; *p; p++)
                putchar(*p == '\t' ? '|' : *p);
        }
        for (i = 0; i < msgs.count; i++)
            print_msg(&msgs.items[i]);
        rc_msglist_free(&msgs);
        if (watch) {
            int secs = optind + 1 < argc ? atoi(argv[optind + 1]) : 5;
            int cycles = optind + 2 < argc ? atoi(argv[optind + 2]) : 0, n;
            for (n = 0; cycles == 0 || n < cycles; n++) {
                RcRoomList rd;
                RcMsgList md;
                sleep((unsigned)(secs > 0 ? secs : 1));
                rc_roomlist_init(&rd);
                rc_msglist_init(&md);
                rc = sess_poll(&s, &rd, &md);
                if (rc != RC_OK) {
                    fprintf(stderr, "rcli: poll: %s\n", rc_error(&s.rc));
                } else {
                    for (i = 0; i < md.count; i++)
                        print_msg(&md.items[i]);
                    for (i = 0; i < md.n_deleted; i++)
                        printf("deleted %s\n", md.deleted[i]);
                    for (i = 0; i < rd.n_removed; i++)
                        printf("room-removed %s\n", rd.removed[i]);
                    for (i = 0; i < rd.count; i++) {
                        printf("room-update ");
                        print_room(&rd.items[i]);
                    }
                }
                fflush(stdout);
                rc_roomlist_free(&rd);
                rc_msglist_free(&md);
            }
        }
    } else if (!strcmp(cmd, "send")) {
        const RcRoom *r;
        Buf text;
        RcMsgList echo;
        int i;
        if (optind + 1 >= argc) {
            usage();
            ret = 64;
            goto done;
        }
        r = sess_find_room(&s, argv[optind]);
        if (!r) {
            fprintf(stderr, "rcli: no such room: %s\n", argv[optind]);
            ret = 1;
            goto done;
        }
        buf_init(&text);
        for (i = optind + 1; i < argc; i++) {
            if (i > optind + 1)
                buf_putc(&text, ' ');
            buf_puts(&text, argv[i]);
        }
        rc_msglist_init(&echo);
        rc = rc_send(&s.rc, r->rid, buf_str(&text), &echo);
        buf_free(&text);
        if (rc != RC_OK)
            ret = fail(&s, rc);
        else if (echo.count)
            print_msg(&echo.items[0]);
        rc_msglist_free(&echo);
    } else {
        usage();
        ret = 64;
    }
done:
    sess_cleanup(&s);
    ar_mem_cleanup();
    return ret;
}

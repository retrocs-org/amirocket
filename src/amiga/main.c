/*
 * AmiRocket - entry point.
 *
 *   AmiRocket                      start the GUI (also from Workbench)
 *   AmiRocket SELFTEST             run the core unit tests, RC 0 = pass
 *   AmiRocket PROBE=<url> [USER=<u> PASSWORD=<p> [CODE=<2fa>]] [INSECURE]
 *                                  check TCP/IP, AmiSSL and the server from
 *                                  the Shell, without opening any window
 */
#include "gui.h"
#include "net_amiga.h"
#include "../core/charset.h"
#include "../core/mem.h"
#include "../core/session.h"
#include "../../tests/test_core.h"

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#ifndef AR_VERSION
#define AR_VERSION "0.1"
#endif
#ifndef AR_DATE
#define AR_DATE "16.11.2026"
#endif

const char ar_version_tag[] = "\0$VER: AmiRocket " AR_VERSION " (" AR_DATE ")";

/* libnix: run main() on a 64 KB stack (TLS and JSON parsing need it) */
unsigned long __stack = 65536;

static void out(const char *s)
{
    PutStr((CONST_STRPTR)s);
}

static void outf(const char *fmt, const char *a)
{
    char b[512];
    snprintf(b, sizeof(b), fmt, a);
    out(b);
}

static void out_latin1(const char *utf8)
{
    Buf b;
    buf_init(&b);
    utf8_to_latin1(&b, utf8, 0);
    out(buf_str(&b));
    buf_free(&b);
}

static int selftest(void)
{
    char err[128];
    int fails = run_core_tests(out);

    /* environment report; missing networking is not a test failure */
    if (net_amiga_open(err, sizeof(err)) == 0) {
        out("bsdsocket.library: ok\n");
        if (net_amiga_tls_available(err, sizeof(err)) == 0)
            outf("AmiSSL: %s\n", net_amiga_tls_version());
        else
            outf("AmiSSL: %s\n", err);
    } else {
        outf("bsdsocket.library: %s\n", err);
    }
    net_amiga_close();
    out(fails ? "SELFTEST FAILED\n" : "SELFTEST PASSED\n");
    return fails ? RETURN_ERROR : RETURN_OK;
}

static int probe(const char *url, const char *user, const char *pass, const char *code, int insecure)
{
    RcSession s;
    char err[160];
    int rc, i, ret = RETURN_OK;

    if (net_amiga_open(err, sizeof(err)) < 0) {
        outf("%s\n", err);
        return RETURN_FAIL;
    }
    if (sess_init(&s, net_amiga(), url, !insecure) != RC_OK) {
        outf("Bad server address: %s\n", rc_error(&s.rc));
        net_amiga_close();
        return RETURN_FAIL;
    }
    outf("Server: %s\n", url);
    rc = rc_server_info(&s.rc);
    if (s.rc.url.tls)
        outf("TLS: %s\n", net_amiga_tls_version());
    if (rc != RC_OK) {
        outf("Server check failed: %s\n", rc_error(&s.rc));
        ret = RETURN_ERROR;
        goto done;
    }
    outf("Rocket.Chat version: %s\n", s.rc.server_version[0] ? s.rc.server_version : "(hidden)");
    if (user && pass) {
        Buf u8u, u8p;   /* Shell arguments are ISO-8859-1 */
        buf_init(&u8u);
        buf_init(&u8p);
        latin1_to_utf8(&u8u, user);
        latin1_to_utf8(&u8p, pass);
        rc = sess_login(&s, buf_str(&u8u), buf_str(&u8p), code, NULL, NULL);
        buf_free(&u8u);
        buf_free(&u8p);
        if (rc != RC_OK) {
            outf("Login failed: %s\n", rc_error(&s.rc));
            ret = RETURN_ERROR;
            goto done;
        }
        outf("Logged in as %s\n", s.rc.username);
        {
            char n[16];
            snprintf(n, sizeof(n), "%d", s.rooms.count);
            outf("Rooms: %s\n", n);
        }
        for (i = 0; i < s.rooms.count && i < 15; i++) {
            out("  ");
            out_latin1(rc_room_title(&s.rooms.items[i]));
            out("\n");
        }
        if (s.rooms.count > 0) {
            RcMsgList ml;
            rc_msglist_init(&ml);
            s.history_count = 5;
            if (sess_open_room(&s, s.rooms.items[0].rid, &ml) == RC_OK) {
                out("Latest messages in ");
                out_latin1(rc_room_title(&s.rooms.items[0]));
                out(":\n");
                for (i = 0; i < ml.count; i++) {
                    out("  <");
                    out_latin1(ml.items[i].username);
                    out("> ");
                    out_latin1(ml.items[i].text);
                    out("\n");
                }
            }
            rc_msglist_free(&ml);
        }
    }
    {
        char st[96];
        snprintf(st, sizeof(st), "HTTP requests: %lu on %lu connection(s)\n",
                 s.rc.http.n_requests, s.rc.http.n_connects);
        out(st);
    }
done:
    sess_cleanup(&s);
    net_amiga_close();
    return ret;
}

int main(int argc, char **argv)
{
    int ret;

    if (ar_mem_init() < 0)
        return RETURN_FAIL;
    if (argc == 0) {   /* started from Workbench */
        ret = gui_run();
    } else {
        enum { A_SELFTEST, A_PROBE, A_USER, A_PASSWORD, A_CODE, A_INSECURE, A_N };
        LONG args[A_N];
        struct RDArgs *rda;

        memset(args, 0, sizeof(args));
        rda = ReadArgs((CONST_STRPTR)"SELFTEST/S,PROBE/K,USER/K,PASSWORD/K,CODE/K,INSECURE/S", args, NULL);
        if (!rda) {
            PrintFault(IoErr(), (CONST_STRPTR)"AmiRocket");
            ar_mem_cleanup();
            return RETURN_FAIL;
        }
        if (args[A_SELFTEST])
            ret = selftest();
        else if (args[A_PROBE])
            ret = probe((const char *)args[A_PROBE], (const char *)args[A_USER],
                        (const char *)args[A_PASSWORD], (const char *)args[A_CODE], args[A_INSECURE] != 0);
        else
            ret = gui_run();
        FreeArgs(rda);
    }
    ar_mem_cleanup();
    return ret;
}

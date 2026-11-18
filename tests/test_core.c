/*
 * Core unit tests. Shared between the host test binary and the Amiga
 * client's SELFTEST mode, so the same checks run on real 68k hardware
 * (big-endian, 32-bit long, no FPU) as on the build machine.
 */
#include "test_core.h"
#include "../sdk/include/amirocket_rexx.h"
#include "../src/core/buf.h"
#include "../src/core/charset.h"
#include "../src/core/json.h"
#include "../src/core/mdtext.h"
#include "../src/core/emoji.h"
#include "../src/core/mem.h"
#include "../src/core/rc_api.h"
#include "../src/core/session.h"
#include "../src/core/timeutil.h"
#include "../src/core/url.h"

#include <stdio.h>
#include <string.h>

static int g_fail, g_pass;
static void (*g_log)(const char *);

static void report(int ok, const char *expr, const char *file, int line)
{
    char b[300];
    if (ok) {
        g_pass++;
        return;
    }
    g_fail++;
    snprintf(b, sizeof(b), "FAIL %s:%d: %s\n", file, line, expr);
    g_log(b);
}

#define CHECK(e) report(!!(e), #e, __FILE__, __LINE__)
#define CHECK_STR(a, b) report(strcmp((a), (b)) == 0, #a " == " #b, __FILE__, __LINE__)

static void test_buf(void)
{
    Buf b;
    int i;

    buf_init(&b);
    CHECK_STR(buf_str(&b), "");
    buf_puts(&b, "hello");
    buf_printf(&b, " %d %s", 42, "x");
    CHECK_STR(buf_str(&b), "hello 42 x");
    for (i = 0; i < 100; i++)
        buf_printf(&b, "%s", "0123456789012345678901234567890123456789");
    CHECK(b.len == 10 + 4000);
    buf_consume(&b, 6);
    CHECK(strncmp(buf_str(&b), "42 x0123", 8) == 0);
    buf_clear(&b);
    {   /* long printf (> internal tmp buffer) */
        char big[600];
        memset(big, 'a', sizeof(big) - 1);
        big[sizeof(big) - 1] = 0;
        buf_printf(&b, "[%s]", big);
        CHECK(b.len == 601);
        CHECK(b.data[600] == ']');
    }
    buf_free(&b);
}

static void test_json(void)
{
    const char *js =
        "{ \"success\": true, \"n\": -1234567890123, \"arr\": [1, {\"a\": \"b\"}, [], \"x\"],"
        "  \"esc\": \"line\\nq\\\"\\u00e5\\ud83d\\ude00\\u20ac\", \"nested\": {\"data\": {\"authToken\": \"T\"}},"
        "  \"nul\": null, \"f\": false, \"k\\u0065y\": 1 }";
    JDoc d;
    char s[64];
    int arr;

    CHECK(json_parse(&d, js, strlen(js)) == 0);
    CHECK(json_type(&d, 0) == JT_OBJECT);
    CHECK(json_bool(&d, json_get(&d, 0, "success"), 0) == 1);
    CHECK(json_bool(&d, json_get(&d, 0, "f"), 1) == 0);
    CHECK(json_ll(&d, json_get(&d, 0, "n"), 0) == -1234567890123LL);
    arr = json_get(&d, 0, "arr");
    CHECK(json_size(&d, arr) == 4);
    CHECK(json_eq(&d, json_get(&d, json_at(&d, arr, 1), "a"), "b"));
    CHECK(json_type(&d, json_at(&d, arr, 2)) == JT_ARRAY);
    CHECK(json_eq(&d, json_at(&d, arr, 3), "x"));
    CHECK(json_at(&d, arr, 4) == -1);
    CHECK(json_str(&d, json_get(&d, 0, "esc"), s, sizeof(s)) == 16);
    CHECK(memcmp(s, "line\nq\"\xC3\xA5\xF0\x9F\x98\x80\xE2\x82\xAC", 16) == 0);
    CHECK(json_eq(&d, json_path(&d, 0, "nested.data.authToken"), "T"));
    CHECK(json_path(&d, 0, "nested.nope.authToken") == -1);
    CHECK(json_is_null(&d, json_get(&d, 0, "nul")));
    CHECK(json_is_null(&d, json_get(&d, 0, "missing")));
    CHECK(json_get(&d, 0, "key") >= 0);   /* escaped key */
    CHECK(json_str(&d, json_get(&d, 0, "esc"), s, 4) == -1);  /* too small */
    json_free(&d);

    CHECK(json_parse(&d, "[1,2", 4) < 0);
    CHECK(json_parse(&d, "{\"a\" 1}", 7) < 0);
    CHECK(json_parse(&d, "{\"a\":1}x", 8) < 0);
    CHECK(json_parse(&d, "\"\x01\"", 3) < 0);
    CHECK(json_parse(&d, "  [ ]  ", 7) == 0 && json_size(&d, 0) == 0);
    json_free(&d);
    {   /* depth limit */
        char deep[200];
        int i;
        for (i = 0; i < 100; i++) {
            deep[i] = '[';
            deep[199 - i] = ']';
        }
        CHECK(json_parse(&d, deep, 200) < 0);
    }
    {
        Buf b;
        buf_init(&b);
        json_put_string(&b, "a\"b\\c\nd\x01" "\xC3\xA5");
        CHECK_STR(buf_str(&b), "\"a\\\"b\\\\c\\nd\\u0001\xC3\xA5\"");
        buf_free(&b);
    }
}

static void test_url(void)
{
    ServerUrl u;
    char err[64];
    Buf b;

    CHECK(url_parse_server("chat.example.com", &u, err, sizeof(err)) == 0);
    CHECK(u.tls == 1 && u.port == 443 && !strcmp(u.host, "chat.example.com") && u.base[0] == 0);
    CHECK(url_parse_server("http://10.0.0.5:3000/", &u, err, sizeof(err)) == 0);
    CHECK(u.tls == 0 && u.port == 3000 && !strcmp(u.host, "10.0.0.5") && u.base[0] == 0);
    CHECK(url_parse_server(" HTTPS://rc.local:8443/chat//", &u, err, sizeof(err)) == 0);
    CHECK(u.tls == 1 && u.port == 8443 && !strcmp(u.base, "/chat"));
    CHECK(url_parse_server("ftp://x", &u, err, sizeof(err)) < 0);
    CHECK(url_parse_server("https://", &u, err, sizeof(err)) < 0);
    CHECK(url_parse_server("https://h:99999", &u, err, sizeof(err)) < 0);
    buf_init(&b);
    url_encode(&b, "2026-11-16T07:00:00.000Z a+b/\xC3\xA5");
    CHECK_STR(buf_str(&b), "2026-11-16T07%3A00%3A00.000Z%20a%2Bb%2F%C3%A5");
    buf_free(&b);
}

static void test_charset(void)
{
    Buf b;

    buf_init(&b);
    utf8_to_latin1(&b, "Hej p\xC3\xA5 dig \xE2\x80\x99q\xE2\x80\x99 \xE2\x80\x9C" "d\xE2\x80\x9D \xE2\x80\x93 caf\xC3\xA9 \xF0\x9F\x98\x80 \xE2\x82\xAC" "5", 0);
    CHECK_STR(buf_str(&b), "Hej p\xE5 dig 'q' \"d\" - caf\xE9 :D EUR5");
    buf_clear(&b);
    utf8_to_latin1(&b, "a\nb\tc\x01", 0);
    CHECK_STR(buf_str(&b), "a b c");
    buf_clear(&b);
    utf8_to_latin1(&b, "a\nb", 1);
    CHECK_STR(buf_str(&b), "a\nb");
    buf_clear(&b);
    /* thumbs up + skin tone + variation selector, unknown CJK, invalid bytes */
    utf8_to_latin1(&b, "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD\xEF\xB8\x8F \xE6\x97\xA5 \xFF\xC3", 0);
    CHECK_STR(buf_str(&b), "(y) ? ??");
    buf_clear(&b);
    latin1_to_utf8(&b, "R\xE4ksm\xF6rg\xE5s \xA9");
    CHECK_STR(buf_str(&b), "R\xC3\xA4ksm\xC3\xB6rg\xC3\xA5s \xC2\xA9");
    buf_free(&b);
}

static void test_time(void)
{
    char iso[32], hm[8], date[16];
    ar_ms t = iso8601_to_ms("2026-11-16T07:21:13.123Z");

    CHECK(t == 1794813673123LL);
    ms_to_iso8601(t, iso);
    CHECK_STR(iso, "2026-11-16T07:21:13.123Z");
    CHECK(iso8601_to_ms("2026-11-16T09:21:13.123+02:00") == t);
    CHECK(iso8601_to_ms("2026-11-16T07:21:13Z") == t - 123);
    CHECK(iso8601_to_ms("1970-01-01T00:00:00.000Z") == 0);
    CHECK(iso8601_to_ms("2000-02-29T00:00:00.5Z") == 951782400500LL);
    CHECK(iso8601_to_ms("garbage") == 0);
    CHECK(http_date_to_ms("Mon, 16 Nov 2026 07:21:13 GMT") == t - 123);
    ms_format_hm(t + 3600000LL, hm);
    CHECK_STR(hm, "08:21");
    ms_format_date(t, date);
    CHECK_STR(date, "2026-11-16");
    ms_to_iso8601(-1, iso);
    CHECK_STR(iso, "1969-12-31T23:59:59.999Z");
}

static RcMsg mk(const char *id, ar_ms ts, const char *text)
{
    RcMsg m;
    memset(&m, 0, sizeof(m));
    m.id = ar_strdup(id);
    m.username = ar_strdup("u");
    m.text = ar_strdup(text);
    m.ts_ms = ts;
    m.updated_ms = ts;
    return m;
}

static void push(RcMsgList *l, RcMsg m)
{
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->items = (RcMsg *)ar_realloc(l->items, sizeof(RcMsg) * (size_t)l->cap);
    }
    l->items[l->count++] = m;
}

static void test_merge(void)
{
    RcMsgList dst, src;

    rc_msglist_init(&dst);
    rc_msglist_init(&src);
    push(&src, mk("c", 300, "three"));
    push(&src, mk("a", 100, "one"));
    push(&src, mk("b", 200, "two"));
    rc_msglist_merge(&dst, &src, 0);
    CHECK(dst.count == 3 && src.count == 0);
    CHECK_STR(dst.items[0].id, "a");
    CHECK_STR(dst.items[2].id, "c");

    push(&src, mk("b", 200, "two edited"));
    src.items[0].updated_ms = 500;
    push(&src, mk("d", 400, "four"));
    src.deleted = (char **)ar_malloc(sizeof(char *));
    src.deleted[0] = ar_strdup("a");
    src.n_deleted = 1;
    rc_msglist_merge(&dst, &src, 2);
    CHECK(dst.count == 2);            /* a deleted, trimmed to newest two */
    CHECK_STR(dst.items[0].id, "c");
    CHECK_STR(dst.items[1].id, "d");
    CHECK(dst.newest_ms == 500);
    rc_msglist_free(&dst);
    rc_msglist_free(&src);

    CHECK(rc_system_text(NULL) == NULL);
    CHECK_STR(rc_system_text("uj"), "joined the room");
    CHECK_STR(rc_system_text("something-new"), "(system event)");
}

static void md_case(const char *in, const char *want, int line)
{
    Buf b;
    buf_init(&b);
    md_to_plain(&b, in);
    report(strcmp(buf_str(&b), want) == 0, in, __FILE__, line);
    if (strcmp(buf_str(&b), want) != 0) {
        g_log("     got: ");
        g_log(buf_str(&b));
        g_log("\n");
    }
    buf_free(&b);
}
#define MD(in, want) md_case(in, want, __LINE__)

static void test_markdown(void)
{
    MD("plain text", "plain text");
    MD("*bold* _italic_ ~strike~", "bold italic strike");
    MD("**strong** __under__ ~~gone~~", "strong under gone");
    MD("a *nested _mix_* end", "a nested mix end");
    MD("2*3*4 and snake_case_name and a * b * c", "2*3*4 and snake_case_name and a * b * c");
    MD("x * not bold* y", "x * not bold* y");
    MD("`*keep* me` done", "*keep* me done");
    MD("see [the docs](https://example.com/a_b_c)", "see the docs (https://example.com/a_b_c)");
    MD("[https://x.org](https://x.org)", "https://x.org");
    MD("https://example.com/some_path_with_underscores_ ok", "https://example.com/some_path_with_underscores_ ok");
    MD("hi :smile: :thumbsup: :+1: :unknown_code: a:b:c",
       "hi \xF0\x9F\x98\x84 \xF0\x9F\x91\x8D \xF0\x9F\x91\x8D :unknown_code: a:b:c");
    MD(":smiling_face_with_3_hearts: :smiling_face_with_hearts: :smiling_face_with_three_hearts:",
       "\xF0\x9F\xA5\xB0 \xF0\x9F\xA5\xB0 \xF0\x9F\xA5\xB0");
    MD("# Title\n## Sub", "Title\nSub");
    MD("before\n```yaml\nkey: *value*\n  - _x_\n```\nafter *b*", "before\nkey: *value*\n  - _x_\nafter b");
    MD("```one line```", "one line");
    MD("line1\n\nline3", "line1\n\nline3");
    MD("", "");
    MD("unclosed `tick and *star", "unclosed `tick and *star");
    MD("\xC3\xA5*\xC3\xA4*", "\xC3\xA5*\xC3\xA4*");      /* no emphasis inside a word */
    MD("*\xC3\xA5ngest*", "\xC3\xA5ngest");
}

static void test_emoji(void)
{
    Buf b;
    int out[8], n;

    CHECK(emoji_by_cp(0x1F604) == 0);
    CHECK(emoji_by_cp(0x1F970) == emoji_by_code("smiling_face_with_3_hearts", 26));
    CHECK(emoji_by_code("smiling_face_with_hearts", 24) == emoji_by_cp(0x1F970));
    CHECK(emoji_by_code("smiling_face_with_three_hearts", 30) == emoji_by_cp(0x1F970));
    CHECK(emoji_by_code("thumbsup", 8) >= 0 && emoji_by_code("+1", 2) == emoji_by_code("thumbsup", 8));
    CHECK(emoji_by_code("nope", 4) == -1);
    n = emoji_complete("smi", out, 8);
    CHECK(n >= 2 && !strcmp(emoji_table[out[0]].code, "smile"));
    n = emoji_complete("thumbs", out, 8);
    CHECK(n == 2);
    buf_init(&b);
    utf8_to_latin1_ex(&b, "hej \xF0\x9F\x98\x84!", 0, 1);
    CHECK(b.len == 7 && b.data[4] == EMOJI_MARK && (unsigned char)b.data[5] == EMOJI_BASE + 0 && b.data[6] == '!');
    buf_clear(&b);
    utf8_to_latin1_ex(&b, "\xF0\x9F\xA5\x82 \xE2\x9D\xA4\xEF\xB8\x8F", 0, 0);
    CHECK_STR(buf_str(&b), "(cheers) <3");
    buf_free(&b);
}

/* Real HTTP framing over an in-memory transport: no sockets required. */
typedef struct { Buf response, request; size_t pos; int connects; const char *followup; int follow_after; } ApiWire;
static int wire_read(void *v, void *buf, int len)
{
    ApiWire *w = v; size_t n = w->response.len - w->pos;
    if (n > (size_t)len) n = (size_t)len;
    memcpy(buf, w->response.data + w->pos, n); w->pos += n; return (int)n;
}
static int wire_write(void *v, const void *buf, int len)
{
    ApiWire *w = v; return buf_append(&w->request, buf, (size_t)len) < 0 ? -1 : len;
}
static int wire_fail_write(void *v,const void *buf,int len) { (void)v; (void)buf; (void)len; return -1; }
static void wire_close(void *v) { (void)v; }
static int wire_connect(void *v, const char *host, int port, const NetOptions *opt,
                        Transport *tr, char *err, size_t size)
{
    ApiWire *w = v; w->pos = 0; w->connects++;
    if (w->followup && w->connects > w->follow_after) { buf_clear(&w->response); buf_puts(&w->response,w->followup); w->followup=NULL; }
    tr->ctx = w; tr->read = wire_read; tr->write = wire_write; tr->close = wire_close; tr->error = NULL;
    return 0;
}
static void wire_response(ApiWire *w, int status, const char *type, const void *body, size_t len)
{
    buf_clear(&w->response); buf_clear(&w->request); w->pos = 0;
    buf_printf(&w->response, "HTTP/1.1 %d test\r\nContent-Length: %lu\r\nContent-Type: %s\r\nConnection: close\r\n\r\n",
               status, (unsigned long)len, type);
    buf_append(&w->response, body, len);
}

static void test_http_send_recovery(void)
{
    ApiWire w={0}; NetIface net={&w,wire_connect}; RcClient c; RcMsgList echo; HttpResponse response;
    buf_init(&w.response); buf_init(&w.request); rc_msglist_init(&echo);
    CHECK(rc_init(&c,&net,"http://chat.test",0)==RC_OK);
    /* An established connection accepts the POST, then supplies no response. */
    c.http.connected=1; c.http.uses=1; c.http.tr.ctx=&w; c.http.tr.read=wire_read;
    c.http.tr.write=wire_write; c.http.tr.close=wire_close;
    CHECK(rc_send(&c,"r1","only once",&echo)==RC_ERR_NET);
    CHECK(w.connects==0 && strstr(c.err,"Delivery unconfirmed") && echo.count==0);
    CHECK(strstr(buf_str(&w.request),"only once")!=NULL);
    /* Safe reads may reconnect once after a stale idle connection. */
    buf_clear(&w.request); buf_clear(&w.response); w.pos=0;
    c.http.connected=1; c.http.uses=1; c.http.tr.ctx=&w;
    c.http.tr.read=wire_read; c.http.tr.write=wire_write; c.http.tr.close=wire_close;
    w.followup="HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}"; w.follow_after=0;
    http_response_init(&response);
    CHECK(http_request(&c.http,"GET","/status",NULL,NULL,0,&response)==0 && w.connects==1);
    http_response_free(&response);
    c.http.connected=1; c.http.uses=1; c.http.tr.ctx=&w;
    c.http.tr.read=wire_read; c.http.tr.write=wire_fail_write; c.http.tr.close=wire_close;
    CHECK(rc_send(&c,"r1","write failed",&echo)==RC_ERR_NET && w.connects==1);
    rc_msglist_free(&echo); rc_cleanup(&c); buf_free(&w.response); buf_free(&w.request);
}

static void test_poll_recovery(void)
{
    ApiWire w={0}; NetIface net={&w,wire_connect}; RcSession session; RcMsgList messages;
    const char *empty="{\"result\":{\"updated\":[],\"deleted\":[]},\"success\":true}";
    buf_init(&w.response); buf_init(&w.request); rc_msglist_init(&messages);
    CHECK(sess_init(&session,&net,"http://chat.test",0)==RC_OK);
    session.logged_in=1; session.foreground=0; strcpy(session.active_rid,"r1"); session.active_last=12345;
    wire_response(&w,503,"text/html","unavailable",11);
    CHECK(sess_poll(&session,NULL,&messages)!=RC_OK && session.active_last==12345 && session.logged_in);
    wire_response(&w,200,"application/json",empty,strlen(empty));
    CHECK(sess_poll(&session,NULL,&messages)==RC_OK && session.active_last==12345);
    wire_response(&w,401,"text/html","expired",7);
    CHECK(sess_poll(&session,NULL,&messages)==RC_ERR_AUTH && session.active_last==12345);
    rc_msglist_free(&messages); sess_cleanup(&session); buf_free(&w.response); buf_free(&w.request);
}

typedef struct { size_t received,total,cancel_at; int calls; } ProgressProbe;
static int probe_progress(void *ctx,size_t received,size_t total)
{
    ProgressProbe *p=ctx; p->received=received; p->total=total; p->calls++;
    return p->cancel_at && received>=p->cancel_at;
}
static void test_transfer_progress(void)
{
    ApiWire w; NetIface net; RcClient c; Buf data; ProgressProbe p={0};
    char body[5000]; int before;
    memset(&w,0,sizeof(w)); memset(body,'x',sizeof(body)); buf_init(&w.response); buf_init(&w.request); buf_init(&data);
    net.ctx=&w; net.connect=wire_connect;
    CHECK(rc_init(&c,&net,"https://chat.test",1)==RC_OK);
    c.http.progress=probe_progress; c.http.progress_ctx=&p;
    wire_response(&w,200,"application/octet-stream",body,sizeof(body));
    CHECK(rc_download(&c,"/file",&data)==RC_OK && data.len==sizeof(body));
    CHECK(p.received==sizeof(body) && p.total==sizeof(body) && p.calls>2);
    buf_free(&data); buf_init(&data); memset(&p,0,sizeof(p)); p.cancel_at=2000;
    wire_response(&w,200,"application/octet-stream",body,sizeof(body)); before=w.connects;
    CHECK(rc_download(&c,"/file",&data)!=RC_OK && strstr(rc_error(&c),"cancelled"));
    CHECK(w.connects==before+1 && data.len==0 && !c.http.connected);
    memset(&p,0,sizeof(p)); c.http.transfer_limit=100;
    wire_response(&w,200,"image/png",body,sizeof(body));
    CHECK(rc_download(&c,"/file",&data)!=RC_OK && strstr(rc_error(&c),"large"));
    c.http.transfer_limit=0; memset(&p,0,sizeof(p));
    buf_clear(&w.response); buf_puts(&w.response,"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n5\r\nhello\r\n0\r\n\r\n"); w.pos=0;
    CHECK(rc_download(&c,"/file",&data)==RC_OK && data.len==5 && p.received==5 && p.total==0);
    rc_cleanup(&c); buf_free(&data); buf_free(&w.response); buf_free(&w.request);
}
static void test_create_conversations(void)
{
    ApiWire w; NetIface net; RcClient c; RcRoomList rooms; int before;
    const char *reply = "{\"success\":true,\"channel\":{\"_id\":\"created-c\"}}";
    memset(&w, 0, sizeof(w)); buf_init(&w.response); buf_init(&w.request);
    net.ctx = &w; net.connect = wire_connect; rc_roomlist_init(&rooms);
    CHECK(rc_init(&c, &net, "https://chat.test/base", 1) == RC_OK);
    strcpy(c.token, "SECRET"); strcpy(c.user_id, "USER");
    wire_response(&w, 201, "application/json", reply, strlen(reply));
    CHECK(rc_create_room(&c, 'c', "amiga-dev", " @alice , bob ", &rooms) == RC_OK);
    CHECK(rooms.count == 1 && !strcmp(rooms.items[0].rid, "created-c") && rooms.items[0].type == 'c');
    CHECK(strstr(buf_str(&w.request), "POST /base/api/v1/channels.create HTTP/1.1") != NULL);
    CHECK(strstr(buf_str(&w.request), "\"members\":[\"alice\",\"bob\"]") != NULL);
    CHECK(strstr(buf_str(&w.request), "X-Auth-Token: SECRET") != NULL);

    rc_roomlist_free(&rooms);
    reply = "{\"success\":true,\"group\":{\"_id\":\"created-p\"}}";
    wire_response(&w, 200, "application/json", reply, strlen(reply));
    CHECK(rc_create_room(&c, 'p', "private-amiga", "", &rooms) == RC_OK);
    CHECK(rooms.count == 1 && rooms.items[0].type == 'p');
    CHECK(strstr(buf_str(&w.request), "POST /base/api/v1/groups.create") != NULL);
    rc_roomlist_free(&rooms);
    reply = "{\"success\":true,\"room\":{\"_id\":\"created-d\"}}";
    wire_response(&w, 200, "application/json", reply, strlen(reply));
    CHECK(rc_create_room(&c, 'd', "", "alice,bob", &rooms) == RC_OK);
    CHECK(rooms.count == 1 && rooms.items[0].type == 'd' && rooms.items[0].member_count == 3);
    CHECK(strstr(buf_str(&w.request), "\"usernames\":\"alice,bob\"") != NULL);
    rc_roomlist_free(&rooms);
    before = w.connects;
    CHECK(rc_create_room(&c, 'd', "", "", &rooms) == RC_ERR_ARGS);
    CHECK(rc_create_room(&c, 'c', "bad name", "", &rooms) == RC_ERR_ARGS);
    CHECK(rc_create_room(&c, 'p', "name", "alice\nInjected", &rooms) == RC_ERR_ARGS);
    CHECK(w.connects == before);
    reply = "{\"success\":false,\"error\":\"error-not-allowed\"}";
    wire_response(&w, 403, "application/json", reply, strlen(reply));
    CHECK(rc_create_room(&c, 'c', "denied", "", &rooms) != RC_OK && rooms.count == 0);
    reply = "{\"success\":true,\"room\":{}}";
    wire_response(&w, 200, "application/json", reply, strlen(reply));
    CHECK(rc_create_room(&c, 'd', "", "alice", &rooms) == RC_ERR_PROTO);
    rc_cleanup(&c); rc_roomlist_free(&rooms); buf_free(&w.response); buf_free(&w.request);
}

static void test_unread_file_metadata(void)
{
    ApiWire w={0}; NetIface net={&w,wire_connect}; RcClient c; RcRoomList rooms; RcMsgList messages;
    const char *subscriptions="{\"success\":true,\"update\":[{\"rid\":\"r\",\"name\":\"general\",\"unread\":5,\"userMentions\":2,\"groupMentions\":1,\"ls\":{\"$date\":1767225600000}}]}";
    const char *files="{\"success\":true,\"messages\":[{\"_id\":\"m\",\"u\":{\"username\":\"alice\"},\"attachments\":[{\"title\":\"a.png\",\"image_url\":\"/a\",\"image_size\":2048},{\"title\":\"b\",\"title_link\":\"/b\",\"size\":-5}]}]}";
    buf_init(&w.response); buf_init(&w.request); rc_roomlist_init(&rooms); rc_msglist_init(&messages);
    CHECK(rc_init(&c,&net,"https://chat.test",1)==RC_OK);
    wire_response(&w,200,"application/json",subscriptions,strlen(subscriptions));
    CHECK(rc_rooms(&c,0,&rooms)==RC_OK && rooms.count==1);
    CHECK(rooms.items[0].mentions==3 && rooms.items[0].last_seen_ms==1767225600000LL && rooms.items[0].unread==5);
    wire_response(&w,200,"application/json",files,strlen(files));
    CHECK(rc_history(&c,"r",'c',20,0,&messages)==RC_OK && messages.count==1);
    CHECK(messages.items[0].file_count==2 && messages.items[0].files[0].size==2048 && messages.items[0].files[1].size==0);
    rc_roomlist_free(&rooms); rc_msglist_free(&messages); rc_cleanup(&c); buf_free(&w.response); buf_free(&w.request);
}

static void test_api_files_history(void)
{
    ApiWire w; NetIface net; RcClient c; Buf file; RcMsgList page, all;
    const char binary[] = {'P','N','G',0,'x',(char)0xff};
    const char *json = "{\"success\":true,\"messages\":[{\"_id\":\"m2\",\"rid\":\"r\",\"msg\":\"two\",\"ts\":\"2026-01-02T00:00:00Z\",\"u\":{\"username\":\"a\"},\"attachments\":[{\"title\":\"one.png\",\"image_url\":\"/file-upload/1/one.png\"},{\"title\":\"two.pdf\",\"title_link\":\"/file-upload/2/two.pdf\"}]},{\"_id\":\"m1\",\"rid\":\"r\",\"msg\":\"one\",\"ts\":\"2026-01-01T00:00:00Z\",\"u\":{\"username\":\"b\"}}]}";
    memset(&w, 0, sizeof(w)); buf_init(&w.response); buf_init(&w.request); buf_init(&file);
    net.ctx = &w; net.connect = wire_connect;
    CHECK(rc_init(&c, &net, "https://chat.test/chat", 1) == RC_OK);
    strcpy(c.token, "SECRET"); strcpy(c.user_id, "USER");
    wire_response(&w, 200, "image/png", binary, sizeof(binary));
    CHECK(rc_download(&c, "/chat/file-upload/x/one.png", &file) == RC_OK);
    CHECK(file.len == sizeof(binary) && !memcmp(file.data, binary, sizeof(binary)));
    CHECK(strstr(buf_str(&w.request), "GET /chat/file-upload/x/one.png HTTP/1.1") != NULL);
    CHECK(strstr(buf_str(&w.request), "X-Auth-Token: SECRET") != NULL);
    CHECK(strstr(buf_str(&w.request), "Cookie: rc_uid=USER; rc_token=SECRET") != NULL);
    {
        const char *second;
        wire_response(&w,302,"image/png","",0);
        buf_clear(&w.response);
        buf_puts(&w.response,"HTTP/1.1 302 Found\r\nLocation: https://storage.test/object?signature=abc\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        w.followup="HTTP/1.1 200 OK\r\nContent-Length: 3\r\nContent-Type: image/png\r\nConnection: close\r\n\r\nPNG";
        w.follow_after=w.connects+1;
        CHECK(rc_download(&c,"/chat/file",&file)==RC_OK && file.len==3);
        second=strstr(buf_str(&w.request),"GET /object?signature=abc ");
        CHECK(second && !strstr(second,"SECRET") && !strstr(second,"Cookie:") && !strstr(second,"X-Auth"));
        wire_response(&w,302,"image/png","",0);
        buf_clear(&w.response);
        buf_puts(&w.response,"HTTP/1.1 302 Found\r\nLocation: http://storage.test/object\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        CHECK(rc_download(&c,"/chat/file",&file)!=RC_OK);
    }

    { const char *bad[] = {"https://evil.test/file", "http://chat.test/chat/file", "https://chat.test:444/file", "//evil.test/file", "javascript:alert(1)", "/file\r\nX: injected", "https://chat.test@evil.test/file"};
      int i, before = w.connects;
      for (i = 0; i < 7; i++) CHECK(rc_download(&c, bad[i], &file) == RC_ERR_ARGS);
      CHECK(w.connects == before);
    }
    wire_response(&w, 302, "image/png", "", 0);
    CHECK(rc_download(&c, "https://chat.test/chat/file", &file) == RC_ERR_HTTP);
    wire_response(&w, 200, "text/html", "login", 5);
    CHECK(rc_download(&c, "/chat/file", &file) == RC_ERR_HTTP);
    wire_response(&w, 200, "application/octet-stream", binary, sizeof(binary));
    CHECK(rc_download(&c, "file-upload/x/file", &file) == RC_OK);
    CHECK(strstr(buf_str(&w.request), "GET /chat/file-upload/x/file ") != NULL);
    CHECK(c.http.max_body == 4UL * 1024UL * 1024UL);
    rc_msglist_init(&page); rc_msglist_init(&all);
    wire_response(&w, 200, "application/json", json, strlen(json));
    CHECK(rc_history(&c, "r", 'c', 50, iso8601_to_ms("2026-01-03T00:00:00Z"), &page) == RC_OK);
    CHECK(strstr(buf_str(&w.request), "latest=2026-01-03T00%3A00%3A00.000Z&inclusive=false") != NULL);
    CHECK(page.count == 2);
    CHECK(rc_msglist_merge(&all, &page, 1000) == 0);
    CHECK(all.count == 2 && !strcmp(all.items[0].id, "m1"));
    CHECK(all.items[1].file_count == 2 && all.items[1].files[0].image && !all.items[1].files[1].image);
    CHECK_STR(all.items[1].files[1].url, "/file-upload/2/two.pdf");
    wire_response(&w, 200, "application/json", json, strlen(json));
    CHECK(rc_history(&c, "r", 'c', 50, 0, &page) == RC_OK);
    CHECK(rc_msglist_merge(&all, &page, 1000) == 0 && all.count == 2);
    rc_msglist_free(&all); rc_msglist_free(&page);
    { const char *fallback = "{\"success\":true,\"messages\":[{\"_id\":\"f\",\"u\":{\"username\":\"a\"},\"file\":{\"_id\":\"file1\",\"name\":\"a b.png\",\"size\":4096,\"type\":\"image/png\"}}]}";
      wire_response(&w, 200, "application/json", fallback, strlen(fallback));
      CHECK(rc_history(&c, "r", 'c', 10, 0, &page) == RC_OK);
      CHECK(page.count == 1 && page.items[0].file_count == 1 && page.items[0].files[0].image);
      CHECK_STR(page.items[0].files[0].url, "file-upload/file1/a%20b.png");
      CHECK(page.items[0].files[0].size==4096);
      rc_msglist_free(&page);
    }
    buf_clear(&w.response); buf_puts(&w.response, "HTTP/1.1 200 OK\r\nContent-Length: 9000000\r\nConnection: close\r\n\r\n");
    CHECK(rc_download(&c, "/chat/file", &file) == RC_ERR_NET);
    CHECK(c.http.max_body == 4UL * 1024UL * 1024UL);
    { char *presence = NULL;
      const char *json = "{\"success\":true,\"users\":[{\"username\":\"alice\",\"status\":\"online\"},{\"username\":\"bob\",\"status\":\"busy\"}]}";
      wire_response(&w,200,"application/json",json,strlen(json));
      CHECK(rc_presence(&c,&presence) == RC_OK);
      CHECK_STR(presence, "alice\tonline\nbob\tbusy\n"); ar_free(presence);
      wire_response(&w,200,"image/png",binary,sizeof(binary));
      CHECK(rc_avatar(&c,"alice",&file) == RC_OK);
      CHECK(strstr(buf_str(&w.request),"GET /chat/api/v1/avatar/alice?format=png&size=32 ") != NULL);
      CHECK(strstr(buf_str(&w.request),"Cookie: rc_uid=USER; rc_token=SECRET") != NULL);
      CHECK(strstr(buf_str(&w.request),"?rc_token=") == NULL);
      CHECK(c.http.max_body == 4UL * 1024UL * 1024UL);
      wire_response(&w,302,"image/png",binary,sizeof(binary)); CHECK(rc_avatar(&c,"alice",&file) == RC_ERR_HTTP);
      wire_response(&w,404,"text/html","missing",7); CHECK(rc_avatar(&c,"alice",&file) == RC_ERR_HTTP);
      CHECK(strstr(buf_str(&w.request),"GET /chat/avatar/alice?format=png&size=32 ") != NULL);
    }
    { RcRoom room; memset(&room,0,sizeof(room)); room.type='d'; room.name=(char *)"alice,bob";
      CHECK(rc_dm_participants(&room,"you") == 3);
      room.name=(char *)"alice,bob,you"; CHECK(rc_dm_participants(&room,"you") == 3);
      room.name=(char *)"alice,bob,charlie"; CHECK(rc_dm_participants(&room,"you") == 4);
      room.member_count=8; CHECK(rc_dm_participants(&room,"you") == 8);
      room.member_count=0; room.name=(char *)"alice"; CHECK(rc_dm_participants(&room,"you") == 0);
    }
    rc_cleanup(&c);
    buf_free(&file); buf_free(&w.request); buf_free(&w.response);
}


static void test_dock_protocol(void)
{
    ArDockEvent e; unsigned long n; char value[80];
    CHECK(ar_rexx_field("unread=17 selected_unread=3\nserver=2 unread=99","unread",value,sizeof(value)) && !strcmp(value,"17"));
    CHECK(!ar_rexx_field("selected_unread=3\nserver=2 unread=99","unread",value,sizeof(value)));
    CHECK(ar_rexx_number("seq=4294967295","seq",&n) && n==4294967295UL);
    CHECK(!ar_rexx_number("unread=-1","unread",&n) && !ar_rexx_number("unread=1x","unread",&n));
    CHECK(ar_rexx_event2("AMIROCKET_EVENT2 instance-1 23 SERVERSTATUS \"2\" \"-2\" \"failed \"\"quoted\"\" text\" dropped=7 truncated=0",&e));
    CHECK(e.sequence==23 && e.dropped==7 && !e.truncated && !strcmp(e.epoch,"instance-1") && !strcmp(e.kind,"SERVERSTATUS"));
    CHECK(ar_rexx_event2("AMIROCKET_EVENT2 newer 1 RESYNC \"\" \"\" \"snapshot required\" dropped=0 truncated=1",&e) && e.truncated);
    CHECK(!ar_rexx_event2("AMIROCKET_EVENT2 e 1 MESSAGE \"unterminated",&e));
    CHECK(!ar_rexx_event2("AMIROCKET_EVENT2 e 1 MESSAGE \"r\" \"u\" \"text\" dropped=0 truncated=9",&e));
    CHECK(!ar_rexx_event2("AMIROCKET_EVENT MESSAGE \"r\" \"u\" \"text\"",&e));
}

int run_core_tests(void (*log)(const char *))
{
    char b[96];

    g_log = log;
    g_fail = g_pass = 0;
    test_buf();
    test_json();
    test_url();
    test_charset();
    test_time();
    test_merge();
    test_markdown();
    test_emoji();
    test_api_files_history();
    test_unread_file_metadata();
    test_create_conversations();
    test_dock_protocol();
    test_transfer_progress();
    test_http_send_recovery();
    test_poll_recovery();
    snprintf(b, sizeof(b), "core tests: %d passed, %d failed\n", g_pass, g_fail);
    log(b);
    return g_fail;
}

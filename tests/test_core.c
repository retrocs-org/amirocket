/*
 * Core unit tests. Shared between the host test binary and the Amiga
 * client's SELFTEST mode, so the same checks run on real 68k hardware
 * (big-endian, 32-bit long, no FPU) as on the build machine.
 */
#include "test_core.h"
#include "../src/core/buf.h"
#include "../src/core/charset.h"
#include "../src/core/json.h"
#include "../src/core/mdtext.h"
#include "../src/core/mem.h"
#include "../src/core/rc_api.h"
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
    MD("hi :smile: :thumbsup: :unknown_code: a:b:c", "hi :) (y) :unknown_code: a:b:c");
    MD("# Title\n## Sub", "Title\nSub");
    MD("before\n```yaml\nkey: *value*\n  - _x_\n```\nafter *b*", "before\nkey: *value*\n  - _x_\nafter b");
    MD("```one line```", "one line");
    MD("line1\n\nline3", "line1\n\nline3");
    MD("", "");
    MD("unclosed `tick and *star", "unclosed `tick and *star");
    MD("\xC3\xA5*\xC3\xA4*", "\xC3\xA5*\xC3\xA4*");      /* no emphasis inside a word */
    MD("*\xC3\xA5ngest*", "\xC3\xA5ngest");
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
    snprintf(b, sizeof(b), "core tests: %d passed, %d failed\n", g_pass, g_fail);
    log(b);
    return g_fail;
}

#include "http.h"
#include "util.h"
#include "traffic.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef AR_VERSION
#define AR_VERSION "development"
#endif

#define MAX_LINE     8192
#define MAX_HEADERS  128

void http_init(HttpClient *c, const NetIface *net, const char *host, int port, int tls, int verify)
{
    memset(c, 0, sizeof(*c));
    c->net = net;
    ar_strlcpy(c->host, host, sizeof(c->host));
    c->port = port;
    c->opt.tls = tls;
    c->opt.verify = verify;
    c->opt.timeout_s = 30;
    ar_strlcpy(c->user_agent, "AmiRocket/" AR_VERSION " (AmigaOS)", sizeof(c->user_agent));
    c->max_body = 4UL * 1024UL * 1024UL;
}

void http_close(HttpClient *c)
{
    if (c->connected && c->tr.close)
        c->tr.close(c->tr.ctx);
    c->connected = 0;
    c->uses = 0;
    c->rpos = c->rlen = 0;
    memset(&c->tr, 0, sizeof(c->tr));
}

void http_response_init(HttpResponse *r)
{
    memset(r, 0, sizeof(*r));
    buf_init(&r->body);
}

void http_response_free(HttpResponse *r)
{
    buf_free(&r->body);
    memset(r, 0, sizeof(*r));
}

typedef struct {
    int got_bytes;   /* any response byte received during this attempt */
} Attempt;

static void set_tr_err(HttpClient *c, const char *what)
{
    const char *detail = (c->tr.error && c->tr.ctx) ? c->tr.error(c->tr.ctx) : NULL;
    if (detail && *detail)
        snprintf(c->err, sizeof(c->err), "%s: %s", what, detail);
    else
        snprintf(c->err, sizeof(c->err), "%s", what);
}

static int report_progress(HttpClient *c)
{
    if(c->progress && c->progress(c->progress_ctx,c->upload_read && c->upload_done?c->upload_length:c->received,c->upload_read?c->upload_length:c->total)) {
        c->cancelled=1; snprintf(c->err,sizeof(c->err),"%s cancelled",c->upload_read?"Upload":"Download"); return -1;
    }
    return 0;
}
static int cancel_wait(void *ctx) { return report_progress((HttpClient *)ctx)<0; }
static int fill(HttpClient *c, Attempt *a)
{
    int n;

    if (c->rpos < c->rlen)
        return 1;
    if(report_progress(c)<0) return -1;
    n = c->tr.read(c->tr.ctx, c->rbuf, (int)sizeof(c->rbuf));
    if (n < 0) {
        set_tr_err(c, "Receive failed");
        return -1;
    }
    if (n == 0) {
        snprintf(c->err, sizeof(c->err), "Connection closed by server");
        return 0;
    }
    a->got_bytes = 1;
    c->rpos = 0;
    c->rlen = n;
    return 1;
}

/* Reads one line without the CRLF into line. Returns 0 ok, -1 failure. */
static int read_line(HttpClient *c, Attempt *a, Buf *line)
{
    buf_clear(line);
    for (;;) {
        char *nl;
        int avail, take, r;

        r = fill(c, a);
        if (r <= 0)
            return -1;
        avail = c->rlen - c->rpos;
        nl = (char *)memchr(c->rbuf + c->rpos, '\n', (size_t)avail);
        take = nl ? (int)(nl - (c->rbuf + c->rpos)) + 1 : avail;
        if (line->len + (size_t)take > MAX_LINE) {
            snprintf(c->err, sizeof(c->err), "HTTP header line too long");
            return -1;
        }
        buf_append(line, c->rbuf + c->rpos, (size_t)take);
        c->rpos += take;
        if (nl)
            break;
    }
    if (line->oom) {
        snprintf(c->err, sizeof(c->err), "Out of memory");
        return -1;
    }
    while (line->len && (line->data[line->len - 1] == '\n' || line->data[line->len - 1] == '\r'))
        line->data[--line->len] = 0;
    return 0;
}

static int body_append(HttpClient *c,Buf *out,const void *data,size_t size)
{
    if(c->stream_body) {
        if(c->body_write(c->body_write_ctx,data,size)<0) {snprintf(c->err,sizeof(c->err),"Could not write downloaded data");return -1;}
    } else if(buf_append(out,data,size)<0) {snprintf(c->err,sizeof(c->err),"Out of memory");return -1;}
    c->body_received+=size;if(!c->upload_read)c->received=c->body_received;
    return report_progress(c);
}
static int read_exact(HttpClient *c, Attempt *a, Buf *out, size_t n, size_t limit)
{
    if (c->body_received>limit || n>limit-c->body_received) {
        snprintf(c->err, sizeof(c->err), "Response too large (%lu bytes)", (unsigned long)(c->body_received+n));
        return -1;
    }
    if (!c->stream_body && buf_reserve(out, n) < 0) {
        snprintf(c->err, sizeof(c->err), "Out of memory");
        return -1;
    }
    while (n) {
        int avail, take, r = fill(c, a);
        if (r <= 0)
            return -1;
        avail = c->rlen - c->rpos;
        take = (size_t)avail < n ? avail : (int)n;
        if(body_append(c,out,c->rbuf+c->rpos,(size_t)take)<0)return -1;
        c->rpos += take;
        n -= (size_t)take;

    }
    return 0;
}

static int read_to_eof(HttpClient *c, Attempt *a, Buf *out, size_t limit)
{
    for (;;) {
        int r = fill(c, a);
        if (r < 0)
            return -1;
        if (r == 0)
            return 0;
        if (c->body_received>limit || (size_t)(c->rlen-c->rpos)>limit-c->body_received) {
            snprintf(c->err, sizeof(c->err), "Response too large");
            return -1;
        }
        if(body_append(c,out,c->rbuf+c->rpos,(size_t)(c->rlen-c->rpos))<0)return -1;
        c->rpos = c->rlen;

        if (out->oom) {
            snprintf(c->err, sizeof(c->err), "Out of memory");
            return -1;
        }
    }
}

static int read_chunked(HttpClient *c, Attempt *a, Buf *out, size_t limit)
{
    Buf line;
    int rc = -1;

    buf_init(&line);
    for (;;) {
        unsigned long sz;
        char *end;

        if (read_line(c, a, &line) < 0)
            goto out;
        sz = strtoul(buf_str(&line), &end, 16);
        if (end == buf_str(&line)) {
            snprintf(c->err, sizeof(c->err), "Bad chunk header");
            goto out;
        }
        if (sz == 0)
            break;
        if (read_exact(c, a, out, (size_t)sz, limit) < 0)
            goto out;
        if (read_line(c, a, &line) < 0)   /* CRLF after chunk data */
            goto out;
    }
    /* trailers */
    for (;;) {
        if (read_line(c, a, &line) < 0)
            goto out;
        if (line.len == 0)
            break;
    }
    rc = 0;
out:
    buf_free(&line);
    return rc;
}

static const char *header_value(const char *line, const char *name)
{
    size_t n = strlen(name);

    if (ar_strncasecmp(line, name, n) != 0 || line[n] != ':')
        return NULL;
    line += n + 1;
    while (*line == ' ' || *line == '\t')
        line++;
    return line;
}

static int contains_token(const char *v, const char *tok)
{
    size_t n = strlen(tok);

    while (*v) {
        while (*v == ' ' || *v == ',' || *v == '\t')
            v++;
        if (ar_strncasecmp(v, tok, n) == 0 && (v[n] == 0 || v[n] == ',' || v[n] == ' ' || v[n] == ';'))
            return 1;
        while (*v && *v != ',')
            v++;
    }
    return 0;
}

static int write_all(HttpClient *c, const char *p, size_t n)
{
    while (n) {
        int w = c->tr.write(c->tr.ctx, p, (int)(n > 32768 ? 32768 : n));
        if (w <= 0) {
            set_tr_err(c, "Send failed");
            return -1;
        }
        c->sent += (size_t)w;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static int ensure_connected(HttpClient *c)
{
    if (c->connected)
        return 0;
    c->rpos = c->rlen = 0;
    c->uses = 0;
    if (c->net->connect(c->net->ctx, c->host, c->port, &c->opt, &c->tr, c->err, sizeof(c->err)) < 0) {
        memset(&c->tr, 0, sizeof(c->tr));
        return -1;
    }
    c->connected = 1;
    c->n_connects++;
    return 0;
}

/* One attempt. *retry is set when a stale persistent connection is the
 * likely cause of the failure, so the caller may reconnect and resend. */
static int attempt(HttpClient *c, const char *method, const char *path, const char *extra,
                   const char *body, size_t body_len, HttpResponse *resp, int *retry)
{
    Buf req, line;
    Attempt a;
    char retry_header[128]="";
    int reused, rc = -1, keep_alive = 1, chunked = 0, http10 = 0, nheaders;
    long long clen = -1;

    c->body_received=0;c->stream_body=0;
    *retry = 0; c->sent=0; c->deferred=0; c->upload_done=0;
    if(c->before_request && c->before_request(c->scheduler_ctx)<0) {
        c->deferred=1; resp->status=429; snprintf(c->err,sizeof(c->err),"Server cooling down; request not sent"); return 0;
    }
    if(report_progress(c)<0) return -1;
    a.got_bytes = 0;
    if (ensure_connected(c) < 0) return -1;
    if(c->tr.interrupt)c->tr.interrupt(c->tr.ctx,c->progress?cancel_wait:NULL,c);
    reused = c->uses > 0;

    buf_init(&req);
    buf_init(&line);
    buf_printf(&req, "%s %s HTTP/1.1\r\n", method, path);
    if ((c->opt.tls && c->port == 443) || (!c->opt.tls && c->port == 80))
        buf_printf(&req, "Host: %s\r\n", c->host);
    else
        buf_printf(&req, "Host: %s:%d\r\n", c->host, c->port);
    buf_printf(&req, "User-Agent: %s\r\nAccept: application/json\r\nConnection: keep-alive\r\n", c->user_agent);
    if (extra)
        buf_puts(&req, extra);
    if(c->upload_read) {
        buf_printf(&req,"Content-Type: %s\r\nContent-Length: %lu\r\n",c->upload_type,(unsigned long)c->upload_length);
    } else if (body) {
        buf_printf(&req, "Content-Type: application/json\r\nContent-Length: %lu\r\n", (unsigned long)body_len);
    } else if (!strcmp(method, "POST") || !strcmp(method, "PUT")) {
        buf_puts(&req, "Content-Length: 0\r\n");
    }
    buf_puts(&req, "\r\n");

    if (req.oom) {
        snprintf(c->err, sizeof(c->err), "Out of memory");
        goto out;
    }

    if (write_all(c, req.data, req.len) < 0) {
        *retry = reused;
        goto out;
    }

    if(c->upload_read) {
        char chunk[4096]; size_t left=c->upload_length;
        while(left) {
            int want=left>sizeof(chunk)?(int)sizeof(chunk):(int)left;
            int got;
            c->received=c->upload_length-left; c->total=c->upload_length;
            if(report_progress(c)<0) goto out;
            got=c->upload_read(c->upload_ctx,chunk,want);
            if(got<=0 || got>want) { snprintf(c->err,sizeof(c->err),"Could not read upload file"); goto out; }
            if(write_all(c,chunk,got)<0) goto out;
            left-=(size_t)got;
        }
        c->upload_done=1; c->received=c->total=c->upload_length;
        if(report_progress(c)<0) goto out;
    } else if(body && body_len && write_all(c,body,body_len)<0) goto out;

    /* Status line (skipping any 1xx interim responses). */
    for (;;) {
        const char *s;
        if (read_line(c, &a, &line) < 0) {
            *retry = reused && !a.got_bytes;
            goto out;
        }
        s = buf_str(&line);
        if (strncmp(s, "HTTP/1.", 7) != 0 || strlen(s) < 12) {
            snprintf(c->err, sizeof(c->err), "Not an HTTP response");
            goto out;
        }
        http10 = s[7] == '0';
        resp->status = atoi(s + 9);
        if (resp->status >= 200 || resp->status < 100)
            break;
        do {   /* discard interim headers */
            if (read_line(c, &a, &line) < 0)
                goto out;
        } while (line.len);
    }
    if (http10)
        keep_alive = 0;

    for (nheaders = 0;; nheaders++) {
        const char *s, *v;
        if (read_line(c, &a, &line) < 0)
            goto out;
        if (line.len == 0)
            break;
        if (nheaders >= MAX_HEADERS) {
            snprintf(c->err, sizeof(c->err), "Too many HTTP headers");
            goto out;
        }
        s = buf_str(&line);
        if ((v = header_value(s, "Content-Length")) != NULL) {
            clen = atol(v);
        } else if ((v = header_value(s, "Transfer-Encoding")) != NULL) {
            chunked = contains_token(v, "chunked");
        } else if ((v = header_value(s, "Connection")) != NULL) {
            if (contains_token(v, "close"))
                keep_alive = 0;
            else if (contains_token(v, "keep-alive"))
                keep_alive = 1;
        } else if ((v = header_value(s, "Content-Type")) != NULL) {
            ar_strlcpy(resp->content_type, v, sizeof(resp->content_type));
        } else if ((v = header_value(s, "Location")) != NULL) {
            ar_strlcpy(resp->location, v, sizeof(resp->location));
        } else if ((v = header_value(s, "Retry-After")) != NULL) {
            ar_strlcpy(retry_header,v,sizeof(retry_header));
        } else if ((v = header_value(s, "Date")) != NULL) {
            resp->date_ms = http_date_to_ms(v);
            if (resp->date_ms)
                c->last_date_ms = resp->date_ms;
        }
    }

    resp->retry_after=traffic_retry_after(retry_header,resp->date_ms?resp->date_ms:c->wall_clock?c->wall_clock(c->scheduler_ctx):c->last_date_ms);
    c->stream_body=c->body_write && resp->status==200 && !strstr(resp->content_type,"text/html");
    if(c->body_write && !c->stream_body && c->max_body>65536)c->max_body=65536;
    c->total=clen>0?(size_t)clen:0;
    if(report_progress(c)<0) goto out;

    if (!strcmp(method, "HEAD") || resp->status == 204 || resp->status == 304) {
        /* no body */
    } else if (chunked) {
        if (read_chunked(c, &a, &resp->body, c->max_body) < 0)
            goto out;
    } else if (clen >= 0) {
        if (read_exact(c, &a, &resp->body, (size_t)clen, c->max_body) < 0)
            goto out;
    } else {
        keep_alive = 0;
        if (read_to_eof(c, &a, &resp->body, c->max_body) < 0)
            goto out;
    }
    if (buf_reserve(&resp->body, 0) < 0) {   /* make sure body.data exists */
        snprintf(c->err, sizeof(c->err), "Out of memory");
        goto out;
    }
    rc = 0;
    c->uses++;
    c->n_requests++;
    if (!keep_alive)
        http_close(c);
out:
    if (rc < 0)
        http_close(c);
    buf_free(&req);
    buf_free(&line);
    return rc;
}

int http_request(HttpClient *c, const char *method, const char *path,
                 const char *extra_headers, const char *body, size_t body_len,
                 HttpResponse *resp)
{
    int retry, rc;

    c->err[0] = 0; c->received=c->total=0; c->cancelled=c->upload_done=c->deferred=0; c->sent=0;
    if(report_progress(c)<0) return -1;
    rc = attempt(c, method, path, extra_headers, body, body_len, resp, &retry);
    if (rc < 0 && retry && !c->cancelled && !(c->body_write && c->body_received) &&
        (!strcmp(method,"GET") || !strcmp(method,"HEAD"))) {
        http_response_free(resp);
        http_response_init(resp);
        rc = attempt(c, method, path, extra_headers, body, body_len, resp, &retry);
    }
    if(c->connected && c->tr.interrupt)c->tr.interrupt(c->tr.ctx,NULL,NULL);
    if(!c->deferred && !c->cancelled && c->after_request) c->after_request(c->scheduler_ctx,rc==0?resp->status:0,resp->retry_after);
    return rc;
}

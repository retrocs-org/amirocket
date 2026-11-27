/*
 * AmiRocket - minimal HTTP/1.1 client with persistent connections.
 *
 * One HttpClient talks to one origin. The connection is kept open between
 * requests (important on a 68020, where a TLS handshake takes seconds) and
 * re-established once for GET/HEAD if the server closed it while idle.
 * Mutating requests are never automatically replayed.
 * Supports Content-Length, chunked and close-delimited bodies.
 */
#ifndef AR_HTTP_H
#define AR_HTTP_H

#include "buf.h"
#include "net.h"
#include "timeutil.h"

typedef struct {
    int   status;
    Buf   body;
    char  content_type[64];
    char  location[2048];
    int retry_after;
    ar_ms date_ms;         /* parsed Date header, 0 if absent */
} HttpResponse;

typedef struct {
    const NetIface *net;
    char  host[256];
    int   port;
    NetOptions opt;

    Transport tr;
    int   connected;
    int   uses;            /* requests served on the current connection */

    char  rbuf[2048];
    int   rpos, rlen;

    char  user_agent[64];
    int (*progress)(void *ctx,size_t received,size_t total); /* nonzero cancels */
    void *progress_ctx;
    size_t received,total,transfer_limit,body_received;
    int stream_body;
    int (*body_write)(void *ctx,const void *data,size_t size);
    void *body_write_ctx;
    int cancelled;
    size_t sent;          /* bytes written in this attempt; nonzero may have committed a POST */
    size_t max_body;       /* responses larger than this are rejected */
    char  err[256];

    /* Optional platform scheduler, called for every wire attempt (including stale GET recovery). */
    int (*before_request)(void *ctx);
    void (*after_request)(void *ctx,int status,int retry_after);
    void *scheduler_ctx;
    ar_ms (*wall_clock)(void *ctx); /* fallback for Retry-After date without Date */
    int deferred;
    int (*upload_read)(void *ctx,void *buffer,int count);
    void *upload_ctx;
    size_t upload_length;
    int upload_done;
    const char *upload_type;

    /* statistics */
    unsigned long n_requests;
    unsigned long n_connects;
    ar_ms last_date_ms;    /* most recent server Date header */
} HttpClient;

void http_init(HttpClient *c, const NetIface *net, const char *host, int port, int tls, int verify);
void http_close(HttpClient *c);

void http_response_init(HttpResponse *r);
void http_response_free(HttpResponse *r);

/* extra_headers: zero or more "Name: value\r\n" lines, or NULL.
 * Returns 0 when a response was received (any status), -1 on transport or
 * protocol failure (c->err describes it). */
int http_request(HttpClient *c, const char *method, const char *path,
                 const char *extra_headers, const char *body, size_t body_len,
                 HttpResponse *resp);

#endif

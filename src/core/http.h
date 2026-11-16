/*
 * AmiRocket - minimal HTTP/1.1 client with persistent connections.
 *
 * One HttpClient talks to one origin. The connection is kept open between
 * requests (important on a 68020, where a TLS handshake takes seconds) and
 * transparently re-established once if the server closed it while idle.
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
    char  location[256];
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
    size_t max_body;       /* responses larger than this are rejected */
    char  err[256];

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

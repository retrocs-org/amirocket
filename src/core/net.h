/*
 * AmiRocket - transport interface.
 *
 * The core never touches sockets or TLS directly. A platform supplies a
 * NetIface that opens a byte stream (plain TCP or TLS) to host:port.
 * POSIX: BSD sockets + OpenSSL. AmigaOS: bsdsocket.library + AmiSSL.
 */
#ifndef AR_NET_H
#define AR_NET_H

#include <stddef.h>
#include "timeutil.h"

typedef struct Transport {
    void *ctx;
    /* >0 bytes read, 0 EOF, -1 error, -2 would-block (nonblocking streams only) */
    int  (*read)(void *ctx, void *buf, int len);
    /* bytes written (may be short), <0 error */
    int  (*write)(void *ctx, const void *buf, int len);
    void (*close)(void *ctx);
    /* optional: last error text for this connection */
    const char *(*error)(void *ctx);
    /* Optional zero-timeout readability check: 1 ready, 0 idle, -1 error. */
    int (*ready)(void *ctx);
    void (*deadline)(void *ctx,ar_ms until); /* absolute monotonic I/O deadline; 0 stream mode */
    void (*interrupt)(void *ctx,int (*cancel)(void *),void *cancel_ctx); /* optional interruptible transfer waits */
} Transport;

typedef struct NetOptions {
    int tls;
    int verify;          /* verify certificate chain and host name */
    int timeout_s;       /* receive/send timeout, 0 = platform default */
    int nonblocking;      /* dedicated WebSocket transport */
    ar_ms deadline_ms;     /* one budget across TCP, TLS and HTTP Upgrade */
} NetOptions;

typedef struct NetIface {
    void *ctx;
    /* 0 on success with *out filled; -1 with err set */
    int (*connect)(void *ctx, const char *host, int port, const NetOptions *opt,
                   Transport *out, char *err, size_t errsz);
    int (*random_bytes)(void *ctx,void *out,int count); /* 0 success; secure entropy */
    int (*sha1)(void *ctx,const void *data,size_t size,unsigned char out[20]);
    ar_ms (*now_ms)(void *ctx); /* monotonic, same domain as deadline_ms */
} NetIface;

#endif

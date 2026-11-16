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

typedef struct Transport {
    void *ctx;
    /* >0 bytes read, 0 orderly EOF, <0 error */
    int  (*read)(void *ctx, void *buf, int len);
    /* bytes written (may be short), <0 error */
    int  (*write)(void *ctx, const void *buf, int len);
    void (*close)(void *ctx);
    /* optional: last error text for this connection */
    const char *(*error)(void *ctx);
} Transport;

typedef struct NetOptions {
    int tls;
    int verify;          /* verify certificate chain and host name */
    int timeout_s;       /* receive/send timeout, 0 = platform default */
} NetOptions;

typedef struct NetIface {
    void *ctx;
    /* 0 on success with *out filled; -1 with err set */
    int (*connect)(void *ctx, const char *host, int port, const NetOptions *opt,
                   Transport *out, char *err, size_t errsz);
} NetIface;

#endif

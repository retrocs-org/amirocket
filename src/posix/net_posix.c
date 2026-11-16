/* AmiRocket - POSIX sockets + OpenSSL transport (host builds and tests). */
#include "net_posix.h"
#include "../core/util.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0   /* macOS: SO_NOSIGPIPE is set on the socket instead */
#endif

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

typedef struct {
    int fd;
    SSL *ssl;
    char err[200];
} PConn;

static SSL_CTX *g_ctx_verify;
static SSL_CTX *g_ctx_noverify;

static const char *ssl_errstr(char *buf, size_t sz)
{
    unsigned long e = ERR_get_error();
    if (!e) {
        snprintf(buf, sz, "%s", errno ? strerror(errno) : "unknown TLS error");
        return buf;
    }
    ERR_error_string_n(e, buf, sz);
    return buf;
}

static int p_read(void *ctx, void *buf, int len)
{
    PConn *c = (PConn *)ctx;
    int n;

    if (c->ssl) {
        n = SSL_read(c->ssl, buf, len);
        if (n > 0)
            return n;
        if (SSL_get_error(c->ssl, n) == SSL_ERROR_ZERO_RETURN)
            return 0;
        if (n == 0)
            return 0;   /* EOF without close_notify; treat as EOF */
        ssl_errstr(c->err, sizeof(c->err));
        return -1;
    }
    do {
        n = (int)recv(c->fd, buf, (size_t)len, 0);
    } while (n < 0 && errno == EINTR);
    if (n < 0)
        snprintf(c->err, sizeof(c->err), "%s", errno == EAGAIN ? "timed out" : strerror(errno));
    return n;
}

static int p_write(void *ctx, const void *buf, int len)
{
    PConn *c = (PConn *)ctx;
    int n;

    if (c->ssl) {
        n = SSL_write(c->ssl, buf, len);
        if (n <= 0) {
            ssl_errstr(c->err, sizeof(c->err));
            return -1;
        }
        return n;
    }
    do {
        n = (int)send(c->fd, buf, (size_t)len, MSG_NOSIGNAL);
    } while (n < 0 && errno == EINTR);
    if (n < 0)
        snprintf(c->err, sizeof(c->err), "%s", strerror(errno));
    return n;
}

static void p_close(void *ctx)
{
    PConn *c = (PConn *)ctx;

    if (c->ssl) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
    }
    if (c->fd >= 0)
        close(c->fd);
    free(c);
}

static const char *p_error(void *ctx)
{
    return ((PConn *)ctx)->err;
}

static SSL_CTX *get_ctx(int verify)
{
    SSL_CTX **slot = verify ? &g_ctx_verify : &g_ctx_noverify;

    if (!*slot) {
        SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx)
            return NULL;
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        if (verify) {
            const char *ca = getenv("AMIROCKET_CA_FILE");
            if (ca)
                SSL_CTX_load_verify_locations(ctx, ca, NULL);
            else
                SSL_CTX_set_default_verify_paths(ctx);
            SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        } else {
            SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
        }
        *slot = ctx;
    }
    return *slot;
}

static int p_connect(void *nctx, const char *host, int port, const NetOptions *opt,
                     Transport *out, char *err, size_t errsz)
{
    struct addrinfo hints, *res = NULL, *ai;
    char portstr[16];
    PConn *c;
    int fd = -1, gai;

    (void)nctx;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%d", port);
    gai = getaddrinfo(host, portstr, &hints, &res);
    if (gai != 0) {
        snprintf(err, errsz, "Cannot resolve %s: %s", host, gai_strerror(gai));
        return -1;
    }
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (opt->timeout_s > 0) {
            struct timeval tv;
            tv.tv_sec = opt->timeout_s;
            tv.tv_usec = 0;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }
#ifdef SO_NOSIGPIPE
        {
            int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
        }
#endif
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        snprintf(err, errsz, "Cannot connect to %s:%d: %s", host, port, strerror(errno));
        return -1;
    }

    c = (PConn *)calloc(1, sizeof(*c));
    if (!c) {
        close(fd);
        snprintf(err, errsz, "Out of memory");
        return -1;
    }
    c->fd = fd;
    if (opt->tls) {
        SSL_CTX *ctx = get_ctx(opt->verify);
        char eb[160];
        if (!ctx) {
            snprintf(err, errsz, "TLS init failed: %s", ssl_errstr(eb, sizeof(eb)));
            p_close(c);
            return -1;
        }
        c->ssl = SSL_new(ctx);
        SSL_set_fd(c->ssl, fd);
        SSL_set_tlsext_host_name(c->ssl, host);
        if (opt->verify)
            SSL_set1_host(c->ssl, host);
        if (SSL_connect(c->ssl) != 1) {
            long vr = SSL_get_verify_result(c->ssl);
            if (opt->verify && vr != X509_V_OK)
                snprintf(err, errsz, "Certificate check failed: %s", X509_verify_cert_error_string(vr));
            else
                snprintf(err, errsz, "TLS handshake failed: %s", ssl_errstr(eb, sizeof(eb)));
            p_close(c);
            return -1;
        }
    }
    out->ctx = c;
    out->read = p_read;
    out->write = p_write;
    out->close = p_close;
    out->error = p_error;
    return 0;
}

static NetIface g_posix_net = { NULL, p_connect };

const NetIface *net_posix(void)
{
    return &g_posix_net;
}

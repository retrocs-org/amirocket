/* AmiRocket - bsdsocket.library (Roadshow, AmiTCP, Miami) + AmiSSL v5. */
#include "net_amiga.h"
#include "../core/mem.h"
#include "../core/util.h"

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>

#include <proto/bsdsocket.h>
#include <amitcp/socketbasetags.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include <proto/amisslmaster.h>
#include <proto/amissl.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>
#include <amissl/amissl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

struct Library *SocketBase;
struct Library *AmiSSLMasterBase;
struct Library *AmiSSLBase;
struct Library *AmiSSLExtBase;

static int g_amissl_open;
static SSL_CTX *g_ctx_verify;
static SSL_CTX *g_ctx_noverify;

/* BSD errno values used by Roadshow / AmiTCP */
static const char *sock_errstr(LONG e)
{
    switch (e) {
    case 4:  return "interrupted";
    case 32: return "broken pipe";
    case 35: return "timed out";          /* EWOULDBLOCK from SO_RCVTIMEO */
    case 50: return "network is down";
    case 51: return "network unreachable";
    case 53: return "connection aborted";
    case 54: return "connection reset by peer";
    case 57: return "socket not connected";
    case 60: return "timed out";
    case 61: return "connection refused";
    case 64: return "host is down";
    case 65: return "no route to host";
    }
    return NULL;
}

static void fmt_sockerr(char *buf, int sz, const char *what)
{
    LONG e = Errno();
    const char *s = sock_errstr(e);
    if (s)
        snprintf(buf, (size_t)sz, "%s: %s", what, s);
    else
        snprintf(buf, (size_t)sz, "%s: error %ld", what, (long)e);
}

int net_amiga_open(char *err, int errsz)
{
    if (SocketBase)
        return 0;
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (!SocketBase) {
        snprintf(err, (size_t)errsz, "No TCP/IP stack is running (bsdsocket.library)");
        return -1;
    }
    /* Ctrl-C to this task aborts a blocking socket call */
    SocketBaseTags(SBTM_SETVAL(SBTC_BREAKMASK), SIGBREAKF_CTRL_C, TAG_END);
    return 0;
}

int net_amiga_tls_available(char *err, int errsz)
{
    if (g_amissl_open)
        return 0;
    if (!SocketBase && net_amiga_open(err, errsz) < 0)
        return -1;
    if (!AmiSSLMasterBase)
        AmiSSLMasterBase = OpenLibrary((CONST_STRPTR)"amisslmaster.library", AMISSLMASTER_MIN_VERSION);
    if (!AmiSSLMasterBase) {
        snprintf(err, (size_t)errsz, "AmiSSL v5 is not installed (amisslmaster.library)");
        return -1;
    }
    /* AMISSL_V303 = AmiSSL 5.1 / OpenSSL 3.0, the oldest API we need. */
    if (OpenAmiSSLTags(AMISSL_V303,
                       AmiSSL_UsesOpenSSLStructs, FALSE,
                       AmiSSL_GetAmiSSLBase, (ULONG)&AmiSSLBase,
                       AmiSSL_GetAmiSSLExtBase, (ULONG)&AmiSSLExtBase,
                       AmiSSL_SocketBase, (ULONG)SocketBase,
                       AmiSSL_ErrNoPtr, (ULONG)&errno,
                       TAG_DONE) != 0) {
        snprintf(err, (size_t)errsz, "AmiSSL could not be initialised (needs AmiSSL 5.1 or newer)");
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
        return -1;
    }
    g_amissl_open = 1;
    return 0;
}

const char *net_amiga_tls_version(void)
{
    if (!g_amissl_open)
        return "not loaded";
    return OpenSSL_version(OPENSSL_VERSION);
}

void net_amiga_close(void)
{
    if (g_ctx_verify) {
        SSL_CTX_free(g_ctx_verify);
        g_ctx_verify = NULL;
    }
    if (g_ctx_noverify) {
        SSL_CTX_free(g_ctx_noverify);
        g_ctx_noverify = NULL;
    }
    if (g_amissl_open) {
        CloseAmiSSL();
        g_amissl_open = 0;
    }
    if (AmiSSLMasterBase) {
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
    }
    if (SocketBase) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}

typedef struct {
    LONG fd;
    SSL *ssl;
    char err[160];
} AConn;

static void ssl_err(AConn *c, const char *what)
{
    unsigned long e = ERR_get_error();
    if (e) {
        char b[120];
        ERR_error_string_n(e, b, sizeof(b));
        snprintf(c->err, sizeof(c->err), "%s: %s", what, b);
    } else {
        fmt_sockerr(c->err, (int)sizeof(c->err), what);
    }
}

static int a_read(void *ctx, void *buf, int len)
{
    AConn *c = (AConn *)ctx;
    LONG n;

    if (c->ssl) {
        int r = SSL_read(c->ssl, buf, len);
        if (r > 0)
            return r;
        if (r == 0 || SSL_get_error(c->ssl, r) == SSL_ERROR_ZERO_RETURN)
            return 0;
        ssl_err(c, "TLS read");
        return -1;
    }
    n = recv(c->fd, buf, len, 0);
    if (n < 0)
        fmt_sockerr(c->err, (int)sizeof(c->err), "recv");
    return (int)n;
}

static int a_write(void *ctx, const void *buf, int len)
{
    AConn *c = (AConn *)ctx;
    LONG n;

    if (c->ssl) {
        int r = SSL_write(c->ssl, buf, len);
        if (r <= 0) {
            ssl_err(c, "TLS write");
            return -1;
        }
        return r;
    }
    n = send(c->fd, (APTR)buf, len, 0);
    if (n < 0)
        fmt_sockerr(c->err, (int)sizeof(c->err), "send");
    return (int)n;
}

static void a_close(void *ctx)
{
    AConn *c = (AConn *)ctx;

    if (c->ssl) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
    }
    if (c->fd >= 0)
        CloseSocket(c->fd);
    ar_free(c);
}

static const char *a_error(void *ctx)
{
    return ((AConn *)ctx)->err;
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
            /* AmiSSL looks in AmiSSL:Certs (installed with AmiSSL) */
            SSL_CTX_set_default_verify_paths(ctx);
            SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        } else {
            SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
        }
        *slot = ctx;
    }
    return *slot;
}

static int a_connect(void *nctx, const char *host, int port, const NetOptions *opt,
                     Transport *out, char *err, size_t errsz)
{
    struct hostent *he;
    struct sockaddr_in sa;
    AConn *c;
    LONG one = 1;

    (void)nctx;
    if (!SocketBase && net_amiga_open(err, (int)errsz) < 0)
        return -1;
    if (opt->tls && net_amiga_tls_available(err, (int)errsz) < 0)
        return -1;

    he = gethostbyname((STRPTR)host);
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
        snprintf(err, errsz, "Cannot resolve host name %s", host);
        return -1;
    }
    c = (AConn *)ar_calloc(1, sizeof(*c));
    if (!c) {
        snprintf(err, errsz, "Out of memory");
        return -1;
    }
    c->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (c->fd < 0) {
        fmt_sockerr(err, (int)errsz, "socket");
        ar_free(c);
        return -1;
    }
    if (opt->timeout_s > 0) {
        struct timeval tv;
        tv.tv_secs = (ULONG)opt->timeout_s;
        tv.tv_micro = 0;
        setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(c->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
    setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    memset(&sa, 0, sizeof(sa));
    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((UWORD)port);
    memcpy(&sa.sin_addr, he->h_addr_list[0], 4);
    if (connect(c->fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        char what[300];
        snprintf(what, sizeof(what), "Cannot connect to %s:%d", host, port);
        fmt_sockerr(err, (int)errsz, what);
        CloseSocket(c->fd);
        ar_free(c);
        return -1;
    }

    if (opt->tls) {
        SSL_CTX *ctx = get_ctx(opt->verify);
        if (!ctx || !(c->ssl = SSL_new(ctx))) {
            snprintf(err, errsz, "TLS setup failed");
            a_close(c);
            return -1;
        }
        SSL_set_fd(c->ssl, (int)c->fd);
        SSL_set_tlsext_host_name(c->ssl, host);
        if (opt->verify)
            SSL_set1_host(c->ssl, host);
        if (SSL_connect(c->ssl) != 1) {
            long vr = SSL_get_verify_result(c->ssl);
            if (opt->verify && vr != X509_V_OK) {
                snprintf(err, errsz, "Certificate check failed: %s", X509_verify_cert_error_string(vr));
            } else {
                ssl_err(c, "TLS handshake failed");
                ar_strlcpy(err, c->err, errsz);
            }
            a_close(c);
            return -1;
        }
    }
    out->ctx = c;
    out->read = a_read;
    out->write = a_write;
    out->close = a_close;
    out->error = a_error;
    return 0;
}

static NetIface g_amiga_net = { NULL, a_connect };

const NetIface *net_amiga(void)
{
    return &g_amiga_net;
}

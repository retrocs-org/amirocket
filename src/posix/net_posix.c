#include <sys/select.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
/* AmiRocket - POSIX sockets + OpenSSL transport (host builds and tests). */
#include "net_posix.h"
#include "../core/util.h"

#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <time.h>
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
    int nonblocking; ar_ms deadline_ms;
} PConn;

static SSL_CTX *g_ctx_verify;
static SSL_CTX *g_ctx_noverify;

/* WebSockets reuse the address selected by a successful REST connection.
 * No second blocking DNS lookup is allowed in the WebSocket handshake. */
typedef struct { char host[256]; int port; struct sockaddr_storage address; socklen_t length; } PCachedAddress;
static PCachedAddress p_addresses[16]; static unsigned p_address_next;
static ar_ms p_now(void *ctx)
{ struct timespec t; (void)ctx; clock_gettime(CLOCK_MONOTONIC,&t); return (ar_ms)t.tv_sec*1000+t.tv_nsec/1000000; }
static void p_deadline(void *ctx,ar_ms until) { ((PConn *)ctx)->deadline_ms=until; }
static int p_wait(PConn *c,int writing,ar_ms until)
{
    for(;;) {
        ar_ms left=until-p_now(NULL); fd_set fds; struct timeval tv; int result;
        if(left<=0) { ar_strlcpy(c->err,"Network deadline exceeded",sizeof(c->err)); return -1; }
        FD_ZERO(&fds); FD_SET(c->fd,&fds); tv.tv_sec=left/1000; tv.tv_usec=(left%1000)*1000;
        result=select(c->fd+1,writing?NULL:&fds,writing?&fds:NULL,NULL,&tv);
        if(result>0) return 0;
        if(result<0 && errno==EINTR) continue;
        ar_strlcpy(c->err,result==0?"Network deadline exceeded":"Socket wait failed",sizeof(c->err)); return -1;
    }
}
static int p_nonblock(PConn *c)
{ int flags=fcntl(c->fd,F_GETFL,0); return flags<0?-1:fcntl(c->fd,F_SETFL,flags|O_NONBLOCK); }
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

static int p_ready(void *ctx)
{
    PConn *c=ctx; fd_set reads; struct timeval tv; int result;
    if(c->ssl && (SSL_pending(c->ssl)>0 || SSL_has_pending(c->ssl))) return 1;
    FD_ZERO(&reads); FD_SET(c->fd,&reads); memset(&tv,0,sizeof(tv));
    result=select(c->fd+1,&reads,NULL,NULL,&tv);
    return result<0?-1:result>0;
}
static int p_random(void *ctx,void *out,int count)
{ (void)ctx;  return RAND_bytes(out,count)==1?0:-1; }
static int p_sha1(void *ctx,const void *data,size_t size,unsigned char out[20])
{ (void)ctx;  return SHA1(data,size,out)?0:-1; }

static int p_read(void *ctx,void *buf,int len)
{
    PConn *c=ctx; int n,want;
    for(;;) {
        if(c->nonblocking && c->deadline_ms && p_now(NULL)>=c->deadline_ms) { ar_strlcpy(c->err,"Network deadline exceeded",sizeof(c->err)); return -1; }
        if(c->ssl) {
            int error; n=SSL_read(c->ssl,buf,len); if(n>0) return n;
            error=SSL_get_error(c->ssl,n); if(error==SSL_ERROR_ZERO_RETURN || !n) return 0;
            want=error==SSL_ERROR_WANT_WRITE;
            if(error!=SSL_ERROR_WANT_READ && !want) { ssl_errstr(c->err,sizeof(c->err)); return -1; }
        } else {
            n=recv(c->fd,buf,len,0); if(n>=0) return n; if(errno==EINTR) continue;
            if(errno!=EAGAIN && errno!=EWOULDBLOCK) { snprintf(c->err,sizeof(c->err),"%s",strerror(errno)); return -1; } want=0;
        }
        if(!c->nonblocking) { ar_strlcpy(c->err,"Read timed out",sizeof(c->err)); return -1; }
        if(!c->deadline_ms) return -2;
        if(p_wait(c,want,c->deadline_ms)) return -1;
    }
}
static int p_write(void *ctx,const void *buf,int len)
{
    PConn *c=ctx; int n,want; ar_ms until=c->deadline_ms?c->deadline_ms:p_now(NULL)+2000;
    for(;;) {
        if(c->nonblocking && p_now(NULL)>=until) { ar_strlcpy(c->err,"Network deadline exceeded",sizeof(c->err)); return -1; }
        if(c->ssl) {
            int error; n=SSL_write(c->ssl,buf,len); if(n>0) return n;
            error=SSL_get_error(c->ssl,n); want=error==SSL_ERROR_WANT_WRITE;
            if(error!=SSL_ERROR_WANT_READ && !want) { ssl_errstr(c->err,sizeof(c->err)); return -1; }
        } else {
            n=send(c->fd,buf,len,MSG_NOSIGNAL); if(n>=0) return n; if(errno==EINTR) continue;
            if(errno!=EAGAIN && errno!=EWOULDBLOCK) { snprintf(c->err,sizeof(c->err),"%s",strerror(errno)); return -1; } want=1;
        }
        if(!c->nonblocking || p_wait(c,want,until)) return -1;
    }
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
    struct addrinfo hints,*res=NULL,*ai,cached; char portstr[16]; PConn *c;
    int gai,i; PCachedAddress *address=NULL;
    (void)nctx;
    c=calloc(1,sizeof(*c)); if(!c) { snprintf(err,errsz,"Out of memory"); return -1; }
    c->fd=-1; c->nonblocking=opt->nonblocking; c->deadline_ms=opt->deadline_ms;
    memset(&hints,0,sizeof(hints)); hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_STREAM;
    if(opt->nonblocking) {
        for(i=0;i<16;i++) if(p_addresses[i].port==port && !strcmp(p_addresses[i].host,host)) { address=&p_addresses[i]; break; }
        if(!address) { snprintf(err,errsz,"WebSocket needs a successful REST connection first"); p_close(c); return -1; }
        memset(&cached,0,sizeof(cached)); cached.ai_family=address->address.ss_family; cached.ai_socktype=SOCK_STREAM;
        cached.ai_addr=(struct sockaddr *)&address->address; cached.ai_addrlen=address->length; ai=&cached;
    } else {
        snprintf(portstr,sizeof(portstr),"%d",port); gai=getaddrinfo(host,portstr,&hints,&res);
        if(gai) { snprintf(err,errsz,"Cannot resolve %s: %s",host,gai_strerror(gai)); p_close(c); return -1; } ai=res;
    }
    for(;ai;ai=ai->ai_next) {
        int connected;
        c->fd=socket(ai->ai_family,ai->ai_socktype,ai->ai_protocol); if(c->fd<0) continue;
        if(opt->timeout_s>0) { struct timeval tv={opt->timeout_s,0}; setsockopt(c->fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv)); setsockopt(c->fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv)); }
#ifdef SO_NOSIGPIPE
        { int one=1; setsockopt(c->fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one)); }
#endif
        if(opt->nonblocking && p_nonblock(c)) { close(c->fd); c->fd=-1; continue; }
        connected=connect(c->fd,ai->ai_addr,ai->ai_addrlen)==0;
        if(!connected && opt->nonblocking && errno==EINPROGRESS && !p_wait(c,1,opt->deadline_ms)) {
            int error=0; socklen_t size=sizeof(error); connected=!getsockopt(c->fd,SOL_SOCKET,SO_ERROR,&error,&size) && !error;
        }
        if(connected) break;
        close(c->fd); c->fd=-1;
    }
    if(c->fd<0) { snprintf(err,errsz,"Cannot connect to %s:%d within deadline",host,port); if(res) freeaddrinfo(res); p_close(c); return -1; }
    if(!opt->nonblocking) {
        PCachedAddress *entry=&p_addresses[p_address_next++%16];
        for(i=0;i<16;i++) if(p_addresses[i].port==port && !strcmp(p_addresses[i].host,host)) { entry=&p_addresses[i]; break; }
        ar_strlcpy(entry->host,host,sizeof(entry->host)); entry->port=port; entry->length=ai->ai_addrlen;
        memcpy(&entry->address,ai->ai_addr,entry->length);
    }
    if(res) freeaddrinfo(res);
    if (opt->tls) {
        SSL_CTX *ctx = get_ctx(opt->verify);
        char eb[160];
        if (!ctx) {
            snprintf(err, errsz, "TLS init failed: %s", ssl_errstr(eb, sizeof(eb)));
            p_close(c);
            return -1;
        }
        c->ssl = SSL_new(ctx);
        if(!c->ssl) { snprintf(err,errsz,"TLS allocation failed"); p_close(c); return -1; }
        SSL_set_fd(c->ssl, c->fd);
        SSL_set_tlsext_host_name(c->ssl, host);
        if (opt->verify)
            SSL_set1_host(c->ssl, host);
        { int result;
          for(;;) { int error; result=SSL_connect(c->ssl); if(result==1 || !opt->nonblocking) break;
              error=SSL_get_error(c->ssl,result);
              if((error!=SSL_ERROR_WANT_READ && error!=SSL_ERROR_WANT_WRITE) || p_wait(c,error==SSL_ERROR_WANT_WRITE,opt->deadline_ms)) break;
          }
        if (result != 1) {
            long vr = SSL_get_verify_result(c->ssl);
            if (opt->verify && vr != X509_V_OK)
                snprintf(err, errsz, "Certificate check failed: %s", X509_verify_cert_error_string(vr));
            else
                snprintf(err, errsz, "TLS handshake failed: %s", ssl_errstr(eb, sizeof(eb)));
            p_close(c);
            return -1;
        }
    }
    }
    out->ctx = c;
    out->read = p_read;
    out->write = p_write;
    out->close = p_close;
    out->error = p_error;
    out->ready = p_ready; out->deadline=p_deadline;
    return 0;
}

static NetIface g_posix_net = { NULL, p_connect, p_random, p_sha1, p_now };

const NetIface *net_posix(void)
{
    return &g_posix_net;
}

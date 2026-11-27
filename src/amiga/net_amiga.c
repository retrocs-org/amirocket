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
#include <sys/ioctl.h>
#include <devices/timer.h>
#include <proto/timer.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include <proto/amisslmaster.h>
#include <proto/amissl.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>
#include <amissl/amissl.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

struct Library *SocketBase;
struct Device *TimerBase;
static struct timerequest a_clock_request;
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
    if(!TimerBase) {
        memset(&a_clock_request,0,sizeof(a_clock_request));
        a_clock_request.tr_node.io_Message.mn_Length=sizeof(a_clock_request);
        if(OpenDevice((CONST_STRPTR)TIMERNAME,UNIT_MICROHZ,(struct IORequest *)&a_clock_request,0)) {
            snprintf(err,(size_t)errsz,"Cannot open monotonic network clock"); return -1;
        }
        TimerBase=a_clock_request.tr_node.io_Device;
    }
    if (SocketBase)
        return 0;
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (!SocketBase) {
        if(TimerBase) { CloseDevice((struct IORequest *)&a_clock_request); TimerBase=NULL; }
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
    if(TimerBase) { CloseDevice((struct IORequest *)&a_clock_request); TimerBase=NULL; }
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
    int nonblocking; ar_ms deadline_ms;
    int (*cancel)(void *);void *cancel_ctx;int previous_nonblocking;ar_ms previous_deadline;
} AConn;

typedef struct { char host[256]; int port; struct sockaddr_in address; } ACachedAddress;
static ACachedAddress a_addresses[16]; static unsigned a_address_next;
static ar_ms a_now(void *ctx)
{
    struct EClockVal v; ULONG frequency; unsigned long long ticks; (void)ctx;
    if(!TimerBase) { char err[128]; if(net_amiga_open(err,sizeof(err))) return 0; }
    frequency=ReadEClock(&v); ticks=((unsigned long long)v.ev_hi<<32)|v.ev_lo;
    return frequency?(ar_ms)(ticks/frequency)*1000+(ar_ms)(ticks%frequency)*1000/frequency:0;
}
static void a_deadline(void *ctx,ar_ms until) { ((AConn *)ctx)->deadline_ms=until; }
static void a_interrupt(void *ctx,int (*cancel)(void *),void *cancel_ctx)
{
    AConn *c=ctx;ULONG mode;
    if(cancel && !c->cancel) {c->previous_nonblocking=c->nonblocking;c->previous_deadline=c->deadline_ms;mode=1;IoctlSocket(c->fd,FIONBIO,(char *)&mode);c->nonblocking=1;}
    if(!cancel && c->cancel) {mode=c->previous_nonblocking;IoctlSocket(c->fd,FIONBIO,(char *)&mode);c->nonblocking=c->previous_nonblocking;c->deadline_ms=c->previous_deadline;}
    c->cancel=cancel;c->cancel_ctx=cancel_ctx;
}
static int a_wait(AConn *c,int writing,ar_ms until)
{
    for(;;) {
        ar_ms left=until-a_now(NULL); fd_set fds; struct timeval tv; LONG result;
        if(c->cancel && c->cancel(c->cancel_ctx)) {ar_strlcpy(c->err,"Transfer cancelled",sizeof(c->err));return -1;}
        if(left<=0) { ar_strlcpy(c->err,"Network deadline exceeded",sizeof(c->err)); return -1; }
        if(c->cancel && left>200)left=200;
        FD_ZERO(&fds); FD_SET(c->fd,&fds); tv.tv_secs=left/1000; tv.tv_micro=(left%1000)*1000;
        result=WaitSelect(c->fd+1,writing?NULL:&fds,writing?&fds:NULL,NULL,&tv,NULL);
        if(result>0) return 0;
        if(result==0 && c->cancel)continue;
        /* Ctrl-C aborts promptly; do not restart an interrupted WaitSelect. */
        ar_strlcpy(c->err,result==0?"Network deadline exceeded":"Socket wait interrupted",sizeof(c->err)); return -1;
    }
}
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

static int a_ready(void *ctx)
{
    AConn *c=ctx; fd_set reads; struct timeval tv; int result;
    if(c->ssl && (SSL_pending(c->ssl)>0 || SSL_has_pending(c->ssl))) return 1;
    FD_ZERO(&reads); FD_SET(c->fd,&reads); memset(&tv,0,sizeof(tv));
    result=WaitSelect(c->fd+1,&reads,NULL,NULL,&tv,NULL);
    return result<0?-1:result>0;
}
static int a_random(void *ctx,void *out,int count)
{ (void)ctx; char err[128]; if(net_amiga_tls_available(err,sizeof(err))) return -1;  return RAND_bytes(out,count)==1?0:-1; }
static int a_sha1(void *ctx,const void *data,size_t size,unsigned char out[20])
{ (void)ctx; char err[128]; if(net_amiga_tls_available(err,sizeof(err))) return -1;  return SHA1(data,size,out)?0:-1; }

static int a_read(void *ctx,void *buf,int len)
{
    AConn *c=ctx; int n,want; ar_ms until=c->deadline_ms?c->deadline_ms:a_now(NULL)+30000;
    for(;;) {
        if(c->cancel && c->cancel(c->cancel_ctx)) {ar_strlcpy(c->err,"Transfer cancelled",sizeof(c->err));return -1;}
        if(c->nonblocking && a_now(NULL)>=until) { ar_strlcpy(c->err,"Network deadline exceeded",sizeof(c->err)); return -1; }
        if(c->ssl) {
            int error; n=SSL_read(c->ssl,buf,len); if(n>0) return n;
            error=SSL_get_error(c->ssl,n); if(error==SSL_ERROR_ZERO_RETURN || !n) return 0;
            want=error==SSL_ERROR_WANT_WRITE;
            if(error!=SSL_ERROR_WANT_READ && !want) { ssl_err(c,"TLS I/O"); return -1; }
        } else {
            n=recv(c->fd,buf,len,0); if(n>=0) return n; if(Errno()==4) continue;
            if(Errno()!=35) { fmt_sockerr(c->err,sizeof(c->err),"Socket I/O"); return -1; } want=0;
        }
        if(!c->nonblocking) { ar_strlcpy(c->err,"Read timed out",sizeof(c->err)); return -1; }
        if(!c->deadline_ms && !c->cancel) return -2;
        if(a_wait(c,want,until)) return -1;
    }
}
static int a_write(void *ctx,const void *buf,int len)
{
    AConn *c=ctx; int n,want; ar_ms until=c->deadline_ms?c->deadline_ms:a_now(NULL)+(c->cancel?30000:2000);
    for(;;) {
        if(c->nonblocking && a_now(NULL)>=until) { ar_strlcpy(c->err,"Network deadline exceeded",sizeof(c->err)); return -1; }
        if(c->ssl) {
            int error; n=SSL_write(c->ssl,buf,len); if(n>0) return n;
            error=SSL_get_error(c->ssl,n); want=error==SSL_ERROR_WANT_WRITE;
            if(error!=SSL_ERROR_WANT_READ && !want) { ssl_err(c,"TLS I/O"); return -1; }
        } else {
            n=send(c->fd,(APTR)buf,len,0); if(n>=0) return n; if(Errno()==4) continue;
            if(Errno()!=35) { fmt_sockerr(c->err,sizeof(c->err),"Socket I/O"); return -1; } want=1;
        }
        if(!c->nonblocking || a_wait(c,want,until)) return -1;
    }
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
    struct hostent *he; ACachedAddress *address=NULL; int i;
    struct sockaddr_in sa;
    AConn *c;
    LONG one = 1;

    (void)nctx;
    if (!SocketBase && net_amiga_open(err, (int)errsz) < 0)
        return -1;
    if (opt->tls && net_amiga_tls_available(err, (int)errsz) < 0)
        return -1;

    memset(&sa,0,sizeof(sa)); sa.sin_len=sizeof(sa); sa.sin_family=AF_INET; sa.sin_port=htons((UWORD)port);
    if(opt->nonblocking) {
        for(i=0;i<16;i++) if(a_addresses[i].port==port && !strcmp(a_addresses[i].host,host)) { address=&a_addresses[i]; break; }
        if(!address) { snprintf(err,errsz,"WebSocket needs a successful REST connection first"); return -1; }
        sa=address->address;
    } else {
        he=gethostbyname((STRPTR)host);
        if(!he || !he->h_addr_list || !he->h_addr_list[0]) { snprintf(err,errsz,"Cannot resolve host name %s",host); return -1; }
        memcpy(&sa.sin_addr,he->h_addr_list[0],4);
    }
    c = (AConn *)ar_calloc(1, sizeof(*c));
    if (!c) {
        snprintf(err, errsz, "Out of memory");
        return -1;
    }
    c->nonblocking=opt->nonblocking; c->deadline_ms=opt->deadline_ms;
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

    if(opt->nonblocking && IoctlSocket(c->fd,FIONBIO,(char *)&one)<0) { snprintf(err,errsz,"Cannot enable bounded socket I/O"); a_close(c); return -1; }
    { int connected=connect(c->fd,(struct sockaddr *)&sa,sizeof(sa))==0;
      if(!connected && opt->nonblocking && Errno()==36 && !a_wait(c,1,opt->deadline_ms)) {
          LONG error=0,size=sizeof(error); connected=getsockopt(c->fd,SOL_SOCKET,SO_ERROR,(char *)&error,&size)==0 && !error;
      }
      if(!connected) { snprintf(err,errsz,"Cannot connect to %s:%d within deadline",host,port); a_close(c); return -1; }
    }
    if(!opt->nonblocking) {
        ACachedAddress *entry=&a_addresses[a_address_next++%16];
        for(i=0;i<16;i++) if(a_addresses[i].port==port && !strcmp(a_addresses[i].host,host)) { entry=&a_addresses[i]; break; }
        ar_strlcpy(entry->host,host,sizeof(entry->host)); entry->port=port; entry->address=sa;
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
        { int result;
          for(;;) { int error; result=SSL_connect(c->ssl); if(result==1 || !opt->nonblocking) break;
              error=SSL_get_error(c->ssl,result);
              if((error!=SSL_ERROR_WANT_READ && error!=SSL_ERROR_WANT_WRITE) || a_wait(c,error==SSL_ERROR_WANT_WRITE,opt->deadline_ms)) break;
          }
        if (result != 1) {
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
    }
    out->ctx = c;
    out->read = a_read;
    out->write = a_write;
    out->close = a_close;
    out->error = a_error;
    out->ready = a_ready; out->deadline=a_deadline; out->interrupt=a_interrupt;
    return 0;
}

static NetIface g_amiga_net = { NULL, a_connect, a_random, a_sha1, a_now };

const NetIface *net_amiga(void)
{
    return &g_amiga_net;
}

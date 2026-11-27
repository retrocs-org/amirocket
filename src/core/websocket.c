#include "websocket.h"
#include "util.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "timeutil.h"
#include "traffic.h"

static void b64(const unsigned char *p,int n,char *out)
{
    static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i,j=0; for(i=0;i<n;i+=3) {
        unsigned v=(unsigned)p[i]<<16; if(i+1<n) v|=(unsigned)p[i+1]<<8; if(i+2<n) v|=p[i+2];
        out[j++]=t[v>>18]; out[j++]=t[(v>>12)&63]; out[j++]=i+1<n?t[(v>>6)&63]:'='; out[j++]=i+2<n?t[v&63]:'=';
    } out[j]=0;
}
void ws_init(WebSocket *w) { memset(w,0,sizeof(*w)); buf_init(&w->input); buf_init(&w->message); }
void ws_close(WebSocket *w)
{
    if(w->tr.close) w->tr.close(w->tr.ctx);
    memset(&w->tr,0,sizeof(w->tr)); w->open=w->fragment=0; buf_free(&w->input); buf_free(&w->message);
    buf_init(&w->input); buf_init(&w->message);
}
static int fail(WebSocket *w,const char *why) { ar_strlcpy(w->err,why,sizeof(w->err)); ws_close(w); return -1; }
static int write_all(WebSocket *w,const void *p,int n)
{
    const char *s=p; while(n>0) { int k=w->tr.write(w->tr.ctx,s,n); if(k<=0 || k>n) return -1; s+=k; n-=k; } return 0;
}
static int frame(WebSocket *w,int op,const void *data,size_t n)
{
    unsigned char h[8],mask[4],block[1024]; size_t at=0; int len=2,i;
    if(!w->open || n>WS_LIMIT || w->net->random_bytes(w->net->ctx,mask,4)) return -1;
    w->tr.deadline(w->tr.ctx,w->net->now_ms(w->net->ctx)+2000);
    h[0]=0x80|op; if(n<126) h[1]=0x80|n;
    else if(n<=65535) { h[1]=0xfe; h[len++]=n>>8; h[len++]=n; }
    else return -1; /* outbound DDP messages are tiny */
    memcpy(h+len,mask,4); len+=4; if(write_all(w,h,len)) return -1;
    while(at<n) { size_t k=n-at; if(k>sizeof(block)) k=sizeof(block);
        for(i=0;i<(int)k;i++) block[i]=((const unsigned char *)data)[at+i]^mask[(at+i)&3];
        if(write_all(w,block,k)) return -1;
        at+=k;
    }
    w->tr.deadline(w->tr.ctx,0); return 0;
}
int ws_text(WebSocket *w,const char *text)
{ return frame(w,1,text,strlen(text))?fail(w,"WebSocket send failed"):0; }
static int token(const char *v,const char *wanted)
{
    size_t n=strlen(wanted); while(*v) { const char *e; while(*v==' ' || *v=='\t' || *v==',') v++; e=v; while(*e && *e!=',' && *e!=' ' && *e!='\t') e++;
        if((size_t)(e-v)==n && !ar_strncasecmp(v,wanted,n)) return 1;
        v=e; } return 0;
}
int ws_open(WebSocket *w,const NetIface *net,const ServerUrl *url,int verify)
{
    unsigned char nonce[16],digest[20]; char key[25],accept[29],joined[80],headers[8193];
    ar_ms until;
    Buf request; NetOptions opt; int used=0,upgrade=0,connection=0,accepted=0; char *line,*end;
    char retry[128]=""; ar_ms date=0;
    ws_close(w); w->err[0]=0; w->status=w->retry_after=0; w->net=net;
    if(!net->now_ms) return fail(w,"WebSocket monotonic clock unavailable");
    until=net->now_ms(net->ctx); if(until<=0) return fail(w,"WebSocket monotonic clock unavailable");
    until+=WS_HANDSHAKE_MS;
    if(strpbrk(url->host,"\r\n\t /:") || strpbrk(url->base,"\r\n\t ")) return fail(w,"Invalid WebSocket origin");
    if(!net->random_bytes || !net->sha1 || net->random_bytes(net->ctx,nonce,16)) return fail(w,"WebSocket crypto unavailable");
    b64(nonce,16,key); snprintf(joined,sizeof(joined),"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",key);
    if(net->sha1(net->ctx,joined,strlen(joined),digest)) return fail(w,"WebSocket crypto unavailable");
    b64(digest,20,accept);
    memset(&opt,0,sizeof(opt)); opt.nonblocking=1; opt.deadline_ms=until;
    opt.tls=url->tls; opt.verify=verify; opt.timeout_s=2;
    if(net->connect(net->ctx,url->host,url->port,&opt,&w->tr,w->err,sizeof(w->err))) { ws_close(w); return -1; }
    if(!w->tr.ready || !w->tr.deadline) return fail(w,"WebSocket readiness unavailable");
    buf_init(&request); buf_printf(&request,"GET %s/websocket HTTP/1.1\r\n",url->base);
    if(url->port==(url->tls?443:80)) buf_printf(&request,"Host: %s\r\n",url->host);
    else buf_printf(&request,"Host: %s:%d\r\n",url->host,url->port);
    buf_printf(&request,"Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: %s\r\n\r\n",key);
    if(request.oom || write_all(w,buf_str(&request),request.len)) { buf_free(&request); return fail(w,"WebSocket handshake send failed"); } buf_free(&request);
    /* Read only through the header delimiter: do not discard a coalesced first frame. */
    while(used<8192) { int n;
        if(net->now_ms(net->ctx)>=until) return fail(w,"WebSocket handshake deadline exceeded");
        n=w->tr.read(w->tr.ctx,headers+used,1); if(n!=1) return fail(w,"WebSocket handshake interrupted");
        used++; if(used>=4 && !memcmp(headers+used-4,"\r\n\r\n",4)) break;
    }
    headers[used]=0; if(used==8192 || strncmp(headers,"HTTP/1.1 ",9) || used<13 || headers[12]!=' ') return fail(w,"Invalid WebSocket status");
    w->status=atoi(headers+9);
    line=strstr(headers,"\r\n")+2;
    while((end=strstr(line,"\r\n")) && end!=line) {
        char *colon; *end=0; colon=strchr(line,':'); if(!colon) return fail(w,"Invalid WebSocket headers"); *colon++=0;
        while(*colon==' ' || *colon=='\t') colon++;
        { char *tail=colon+strlen(colon); while(tail>colon && (tail[-1]==' ' || tail[-1]=='\t')) *--tail=0; }
        if(!ar_strcasecmp(line,"Date")) date=http_date_to_ms(colon);
        if(!ar_strcasecmp(line,"Retry-After")) ar_strlcpy(retry,colon,sizeof(retry));
        if(!ar_strcasecmp(line,"Upgrade")) upgrade=token(colon,"websocket");
        if(!ar_strcasecmp(line,"Connection")) connection=token(colon,"upgrade");
        if(!ar_strcasecmp(line,"Sec-WebSocket-Accept")) accepted=!strcmp(colon,accept);
        if(!ar_strcasecmp(line,"Sec-WebSocket-Extensions") || !ar_strcasecmp(line,"Sec-WebSocket-Protocol")) return fail(w,"Unexpected WebSocket extension");
        line=end+2;
    }
    if(w->status!=101) {
        w->retry_after=traffic_retry_after(retry,date);
        return fail(w,"WebSocket upgrade rejected; using polling");
    }
    if(!upgrade || !connection || !accepted) return fail(w,"Invalid WebSocket upgrade response");
    w->tr.deadline(w->tr.ctx,0); w->open=1; return 0;
}
static int valid_utf8(const unsigned char *p,size_t n)
{
    size_t i=0; while(i<n) {
        unsigned c=p[i++],v; int count,j;
        if(c<128) continue;
        if(c>=0xc2 && c<=0xdf) { v=c&31; count=1; }
        else if(c>=0xe0 && c<=0xef) { v=c&15; count=2; }
        else if(c>=0xf0 && c<=0xf4) { v=c&7; count=3; }
        else return 0;
        if(n-i<(size_t)count) return 0;
        for(j=0;j<count;j++) { if((p[i]&0xc0)!=0x80) return 0; v=(v<<6)|(p[i++]&63); }
        if((count==2 && v<0x800) || (count==3 && v<0x10000) || v>0x10ffff || (v>=0xd800 && v<=0xdfff)) return 0;
    } return 1;
}
static void consume(Buf *b,size_t n) { memmove(b->data,b->data+n,b->len-n); b->len-=n; b->data[b->len]=0; if(!b->len && b->cap>8192) { buf_free(b); buf_init(b); } }
int ws_read(WebSocket *w)
{
    int loops=0; if(!w->open) return -1;
    while(loops++<16) {
        const unsigned char *p=(const unsigned char *)w->input.data; size_t n=0,h=2; int op=0,fin=0,complete=0;
        if(w->input.len>=2) {
            op=p[0]&15; fin=p[0]&128; n=p[1]&127;
            if((p[0]&0x70) || (p[1]&128) || !(op==0 || op==1 || op==8 || op==9 || op==10)) return fail(w,"Invalid WebSocket frame");
            if(n==126) { h=4; if(w->input.len>=4) n=((size_t)p[2]<<8)|p[3]; }
            if(n==127) { int i; h=10; if(w->input.len>=10) { n=0; for(i=2;i<10;i++) { if(n>WS_LIMIT/256) return fail(w,"WebSocket frame too large"); n=n*256+p[i]; } } }
            if(w->input.len>=h) {
                if((h==4 && n<126) || (h==10 && n<65536) || n>WS_LIMIT || (op>=8 && (!fin || n>125))) return fail(w,"WebSocket frame too large");
                complete=w->input.len-h>=n;
            }
        }
        if(complete) {
            if(op==8) { if(n!=1) frame(w,8,p+h,n); return fail(w,"WebSocket closed; using polling"); }
            if(op==9) { if(frame(w,10,p+h,n)) return fail(w,"WebSocket pong failed"); consume(&w->input,h+n); continue; }
            if(op==10) { consume(&w->input,h+n); continue; }
            if((op==0 && !w->fragment) || (op==1 && w->fragment)) return fail(w,"Invalid WebSocket continuation");
            if(op==1) buf_clear(&w->message);
            if(w->message.len+n>WS_LIMIT) return fail(w,"WebSocket message too large");
            buf_append(&w->message,p+h,n); if(w->message.oom) return fail(w,"WebSocket out of memory");
            consume(&w->input,h+n); w->fragment=!fin; if(fin) { if(!valid_utf8((const unsigned char *)w->message.data,w->message.len)) return fail(w,"Invalid WebSocket UTF-8"); return 1; }
        } else {
            char block[4096]; int ready=w->tr.ready(w->tr.ctx),got;
            if(ready<0) return fail(w,"WebSocket readiness failed");
            if(!ready) return 0;
            got=w->tr.read(w->tr.ctx,block,sizeof(block)); if(got==-2) return 0; if(got<=0) return fail(w,"WebSocket read failed");
            if(w->input.len+(size_t)got>WS_LIMIT+4096) return fail(w,"WebSocket input too large");
            buf_append(&w->input,block,got); if(w->input.oom) return fail(w,"WebSocket out of memory");
        }
    } return 0;
}

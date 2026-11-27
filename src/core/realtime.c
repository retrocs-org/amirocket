#include "realtime.h"
#include "json.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
void realtime_init(RcRealtime *r) { memset(r,0,sizeof(*r)); ws_init(&r->ws); }
void realtime_close(RcRealtime *r) { ws_close(&r->ws); r->phase=r->ready_mask=0; r->ready_since=0; r->room[0]=r->subscription[0]=r->deletion[0]=0; }
int realtime_ready(const RcRealtime *r) { return r->phase==4 && r->ready_mask==15; }
void realtime_failed(RcRealtime *r,ar_ms now)
{
    int seconds; realtime_close(r); if(r->failures<6) r->failures++;
    seconds=5<<(r->failures-1); if(seconds>300) seconds=300;
    if(r->ws.retry_after>seconds) seconds=r->ws.retry_after;
    r->retry_at=now+(ar_ms)seconds*1000; r->dirty_messages=r->dirty_rooms=1;
}
static int send_buf(RcRealtime *r,Buf *b)
{ int rc=b->oom?-1:ws_text(&r->ws,buf_str(b)); buf_free(b); return rc; }
int realtime_start(RcRealtime *r,RcClient *c,ar_ms now)
{
    ar_ms began=c->http.net->now_ms?c->http.net->now_ms(c->http.net->ctx):0;
    if(ws_open(&r->ws,c->http.net,&c->url,c->http.opt.verify) ||
       ws_text(&r->ws,"{\"msg\":\"connect\",\"version\":\"1\",\"support\":[\"1\"]}")) {
        ar_ms elapsed=began?c->http.net->now_ms(c->http.net->ctx)-began:0;
        realtime_failed(r,now+(elapsed>0?elapsed:0)); return -1;
    }
    if(began) { ar_ms elapsed=c->http.net->now_ms(c->http.net->ctx)-began; if(elapsed>0) now+=elapsed; }
    r->phase=1; r->deadline=now+15000; r->last_receive=now; return 0;
}
static int sub(RcRealtime *r,const char *id,const char *name,const char *event)
{
    Buf b; buf_init(&b); buf_puts(&b,"{\"msg\":\"sub\",\"id\":"); json_put_string(&b,id);
    buf_puts(&b,",\"name\":"); json_put_string(&b,name); buf_puts(&b,",\"params\":["); json_put_string(&b,event);
    buf_puts(&b,",false]}"); return send_buf(r,&b);
}
static int room_sub(RcRealtime *r,const char *room,ar_ms now)
{
    Buf b; char event[128];
    if(r->subscription[0]) {
        buf_init(&b); buf_puts(&b,"{\"msg\":\"unsub\",\"id\":"); json_put_string(&b,r->subscription); buf_putc(&b,'}'); if(send_buf(r,&b)) return -1;
    }
    if(r->deletion[0]) {
        buf_init(&b); buf_puts(&b,"{\"msg\":\"unsub\",\"id\":"); json_put_string(&b,r->deletion); buf_putc(&b,'}'); if(send_buf(r,&b)) return -1;
    }
    r->subscription[0]=r->deletion[0]=0; ar_strlcpy(r->room,room,sizeof(r->room));
    r->ready_mask&=3;
    if(!*room) r->ready_mask|=12;
    else {
        snprintf(r->subscription,sizeof(r->subscription),"room-%lu",++r->serial);
        snprintf(r->deletion,sizeof(r->deletion),"delete-%lu",r->serial);
        snprintf(event,sizeof(event),"%s/deleteMessage",room);
        if(sub(r,r->subscription,"stream-room-messages",room) || sub(r,r->deletion,"stream-notify-room",event)) return -1;
    }
    r->deadline=now+15000; return 0;
}
static int handle(RcRealtime *r,RcClient *c,JDoc *d,const char *room,ar_ms now)
{
    int msg=json_get(d,0,"msg"),id=json_get(d,0,"id");
    if(json_eq(d,msg,"ping")) {
        Buf b; char ping[128]; buf_init(&b); buf_puts(&b,"{\"msg\":\"pong\"");
        if(json_str(d,id,ping,sizeof(ping))>=0) { buf_puts(&b,",\"id\":"); json_put_string(&b,ping); }
        buf_putc(&b,'}'); return send_buf(r,&b);
    }
    if(json_eq(d,msg,"failed") || json_eq(d,msg,"error")) return -1;
    if(r->phase==1 && json_eq(d,msg,"connected")) {
        Buf b; buf_init(&b); buf_puts(&b,"{\"msg\":\"method\",\"method\":\"login\",\"id\":\"login\",\"params\":[{\"resume\":"); json_put_string(&b,c->token); buf_puts(&b,"}]}");
        if(send_buf(r,&b)) return -1;
        r->phase=2; r->deadline=now+15000;
    } else if(r->phase==2 && json_eq(d,msg,"result") && json_eq(d,id,"login")) {
        char event[128];
        if(!json_is_null(d,json_get(d,0,"error")) || !json_eq(d,json_path(d,0,"result.id"),c->user_id)) return -1;
        snprintf(event,sizeof(event),"%s/subscriptions-changed",c->user_id); if(sub(r,"subscriptions","stream-notify-user",event)) return -1;
        snprintf(event,sizeof(event),"%s/rooms-changed",c->user_id); if(sub(r,"rooms","stream-notify-user",event)) return -1;
        r->phase=3; r->ready_mask=0; if(room_sub(r,room,now)) return -1;
    } else if(r->phase>=3 && json_eq(d,msg,"ready")) {
        int a=json_get(d,0,"subs"),i; for(i=0;i<json_size(d,a);i++) {
            int t=json_at(d,a,i); if(json_eq(d,t,"subscriptions")) r->ready_mask|=1;
            if(json_eq(d,t,"rooms")) r->ready_mask|=2;
            if(r->subscription[0] && json_eq(d,t,r->subscription)) r->ready_mask|=4;
            if(r->deletion[0] && json_eq(d,t,r->deletion)) r->ready_mask|=8;
        }
        if(r->ready_mask==15) { r->phase=4; if(!r->ready_since) r->ready_since=now; r->dirty_messages=r->dirty_rooms=1; }
    } else if(r->phase>=3 && json_eq(d,msg,"nosub")) {
        if(json_eq(d,id,"subscriptions") || json_eq(d,id,"rooms") || (r->subscription[0] && json_eq(d,id,r->subscription)) || (r->deletion[0] && json_eq(d,id,r->deletion))) return -1;
    } else if(r->phase>=3 && json_eq(d,msg,"changed")) {
        char event[128];
        if(json_eq(d,json_get(d,0,"collection"),"stream-room-messages") && json_eq(d,json_path(d,0,"fields.eventName"),r->room)) r->dirty_messages=1;
        snprintf(event,sizeof(event),"%s/deleteMessage",r->room);
        if(r->room[0] && json_eq(d,json_get(d,0,"collection"),"stream-notify-room") && json_eq(d,json_path(d,0,"fields.eventName"),event)) r->dirty_messages=1;
        if(json_eq(d,json_get(d,0,"collection"),"stream-notify-user")) {
            snprintf(event,sizeof(event),"%s/subscriptions-changed",c->user_id);
            if(json_eq(d,json_path(d,0,"fields.eventName"),event)) r->dirty_rooms=1;
            snprintf(event,sizeof(event),"%s/rooms-changed",c->user_id);
            if(json_eq(d,json_path(d,0,"fields.eventName"),event)) r->dirty_rooms=1;
        }
    }
    return 0;
}
int realtime_step(RcRealtime *r,RcClient *c,const char *room,ar_ms now)
{
    int i; ar_ms began=c->http.net->now_ms?c->http.net->now_ms(c->http.net->ctx):0;
    if(!r->phase) return 0;
    if(r->phase>=3 && strcmp(room,r->room) && room_sub(r,room,now)) goto broken;
    for(i=0;i<16;i++) {
        JDoc d; int got=ws_read(&r->ws); if(got<0) goto broken; if(!got) break;
        r->last_receive=now;
        if(memchr(r->ws.message.data,0,r->ws.message.len) || json_parse(&d,buf_str(&r->ws.message),r->ws.message.len)) goto broken;
        got=handle(r,c,&d,room,now); json_free(&d);
        buf_free(&r->ws.message); buf_init(&r->ws.message); if(got) goto broken;
    }
    if((!realtime_ready(r) && now>=r->deadline) || now-r->last_receive>90000) goto broken;
    if(realtime_ready(r) && r->ready_since && now-r->ready_since>60000) r->failures=0;
    return 0;
broken:
    if(began) { ar_ms elapsed=c->http.net->now_ms(c->http.net->ctx)-began; if(elapsed>0) now+=elapsed; }
    realtime_failed(r,now); return -1;
}

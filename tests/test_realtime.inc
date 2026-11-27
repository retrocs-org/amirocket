/* Memory transport exercises framing and DDP independently of socket timing. */
#include "../src/core/realtime.h"
typedef struct { Buf in,out; size_t pos; int slice,closed; ar_ms ticks,read_tick; } WsWire;
static int ws_wire_read(void *ctx,void *out,int n)
{ WsWire *w=ctx; w->ticks+=w->read_tick; if((size_t)n>w->in.len-w->pos) n=w->in.len-w->pos; if(w->slice && n>w->slice) n=w->slice; memcpy(out,w->in.data+w->pos,n); w->pos+=n; return n; }
static int ws_wire_write(void *ctx,const void *p,int n) { buf_append(&((WsWire *)ctx)->out,p,n); return n; }
static int ws_wire_ready(void *ctx) { WsWire *w=ctx; return w->pos<w->in.len; }
static void ws_wire_close(void *ctx) { ((WsWire *)ctx)->closed++; }
static void ws_wire_deadline(void *ctx,ar_ms until) { (void)ctx; (void)until; }
static ar_ms ws_wire_now(void *ctx) { return 1000+((WsWire *)ctx)->ticks; }
static int ws_wire_connect(void *ctx,const char *host,int port,const NetOptions *opt,Transport *out,char *err,size_t size)
{ memset(out,0,sizeof(*out)); out->ctx=ctx; out->read=ws_wire_read; out->write=ws_wire_write; out->ready=ws_wire_ready; out->close=ws_wire_close; out->deadline=ws_wire_deadline; return 0; }
static int ws_wire_random(void *ctx,void *out,int n)
{ if(n==16) memcpy(out,"the sample nonce",16); else memset(out,0x37,n); return 0; }
static int ws_wire_sha(void *ctx,const void *p,size_t n,unsigned char out[20])
{
    static const unsigned char digest[]={0xb3,0x7a,0x4f,0x2c,0xc0,0x62,0x4f,0x16,0x90,0xf6,0x46,0x06,0xcf,0x38,0x59,0x45,0xb2,0xbe,0xc4,0xea};
    if(n!=60 || memcmp(p,"dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11",60)) return -1;
    memcpy(out,digest,20); return 0;
}
static void ws_wire_upgrade(WsWire *w,int valid)
{
    buf_clear(&w->in); buf_clear(&w->out); w->pos=0;
    buf_puts(&w->in,"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Accept: ");
    buf_puts(&w->in,valid?"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=":"invalid"); buf_puts(&w->in,"\r\n\r\n");
}
static void ws_wire_frame(WsWire *w,int op,int fin,const char *text)
{
    unsigned char h[4]; size_t n=strlen(text); h[0]=(fin?128:0)|op;
    if(n<126) { h[1]=n; buf_append(&w->in,h,2); }
    else { h[1]=126; h[2]=n>>8; h[3]=n; buf_append(&w->in,h,4); }
    buf_puts(&w->in,text);
}
static void test_realtime(void)
{
    WsWire wire={0}; NetIface net={&wire,ws_wire_connect,ws_wire_random,ws_wire_sha,ws_wire_now}; WebSocket ws; ServerUrl url; RcRealtime rt; RcClient client;
    char err[80]; int i;
    buf_init(&wire.in); buf_init(&wire.out); ws_init(&ws); realtime_init(&rt);
    CHECK(!url_parse_server("https://chat.test/sub/",&url,err,sizeof(err)));
    ws_wire_upgrade(&wire,1); CHECK(!ws_open(&ws,&net,&url,1));
    CHECK(strstr(buf_str(&wire.out),"GET /sub/websocket HTTP/1.1") && strstr(buf_str(&wire.out),"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ=="));
    CHECK(strstr(buf_str(&wire.out),"Host: chat.test\r\n") && !strstr(buf_str(&wire.out),"chat.test:443"));
    CHECK(ws_read(&ws)==0);
    ws_wire_frame(&wire,1,0,"hel"); ws_wire_frame(&wire,9,1,"ping"); ws_wire_frame(&wire,0,1,"lo");
    wire.slice=1; for(i=0;i<20;i++) if(ws_read(&ws)==1) break;
    CHECK(i<20 && !strcmp(buf_str(&ws.message),"hello"));
    CHECK(wire.out.len>0 && ws.open); /* control ping interleaved with fragments */
    ws_wire_frame(&wire,0,1,"unexpected"); for(i=0;i<10 && ws.open;i++) ws_read(&ws);
    CHECK(!ws.open && strstr(ws.err,"continuation")); wire.slice=0;
    ws_wire_upgrade(&wire,0); CHECK(ws_open(&ws,&net,&url,1)<0 && !ws.open);
    ws_wire_upgrade(&wire,1); CHECK(!ws_open(&ws,&net,&url,1));
    { const unsigned char masked[]={0x81,0x80}; buf_append(&wire.in,masked,2); CHECK(ws_read(&ws)<0); }
    ws_wire_upgrade(&wire,1); CHECK(!ws_open(&ws,&net,&url,1));
    { const unsigned char huge[]={0x81,127,0,0,0,0,0,2,0,1}; buf_append(&wire.in,huge,sizeof(huge)); CHECK(ws_read(&ws)<0); }
    ws_wire_upgrade(&wire,1); CHECK(!ws_open(&ws,&net,&url,1));
    { const unsigned char invalid[]={0x81,2,0xc0,0x80}; buf_append(&wire.in,invalid,sizeof(invalid)); CHECK(ws_read(&ws)<0 && strstr(ws.err,"UTF-8")); }
    ws_wire_upgrade(&wire,1); CHECK(!ws_open(&ws,&net,&url,1));
    { const unsigned char badping[]={0x09,0}; buf_append(&wire.in,badping,sizeof(badping)); CHECK(ws_read(&ws)<0); }
    buf_clear(&wire.in); buf_clear(&wire.out); wire.pos=0;
    buf_puts(&wire.in,"HTTP/1.1 429 Too Many Requests\r\nRetry-After: 120\r\n\r\n");
    CHECK(ws_open(&ws,&net,&url,1)<0 && ws.status==429 && ws.retry_after==120);
    ws_wire_upgrade(&wire,1); wire.read_tick=200;
    CHECK(ws_open(&ws,&net,&url,1)<0 && strstr(ws.err,"deadline") && wire.ticks<=WS_HANDSHAKE_MS+400);
    wire.read_tick=wire.ticks=0;
    CHECK(!rc_init(&client,&net,"https://chat.test/sub",1)); strcpy(client.user_id,"U"); strcpy(client.token,"SECRET");
    ws_wire_upgrade(&wire,1); CHECK(!realtime_start(&rt,&client,1000) && rt.phase==1);
    ws_wire_frame(&wire,1,1,"{\"msg\":\"connected\",\"session\":\"x\"}");
    CHECK(!realtime_step(&rt,&client,"r1",2000) && rt.phase==2);
    ws_wire_frame(&wire,1,1,"{\"msg\":\"result\",\"id\":\"login\",\"result\":{\"id\":\"U\"}}");
    CHECK(!realtime_step(&rt,&client,"r1",3000) && rt.phase==3 && !realtime_ready(&rt));
    ws_wire_frame(&wire,1,1,"{\"msg\":\"ready\",\"subs\":[\"subscriptions\",\"rooms\",\"room-1\",\"delete-1\"]}");
    CHECK(!realtime_step(&rt,&client,"r1",4000) && realtime_ready(&rt) && rt.dirty_rooms && rt.dirty_messages);
    rt.dirty_rooms=rt.dirty_messages=0;
    ws_wire_frame(&wire,1,1,"{\"msg\":\"changed\",\"collection\":\"stream-room-messages\",\"fields\":{\"eventName\":\"other\"}}");
    CHECK(!realtime_step(&rt,&client,"r1",5000) && !rt.dirty_messages);
    ws_wire_frame(&wire,1,1,"{\"msg\":\"changed\",\"collection\":\"stream-room-messages\",\"fields\":{\"eventName\":\"r1\"}}");
    CHECK(!realtime_step(&rt,&client,"r1",6000) && rt.dirty_messages);
    ws_wire_frame(&wire,1,1,"{\"msg\":\"changed\",\"collection\":\"stream-notify-user\",\"fields\":{\"eventName\":\"U/subscriptions-changed\"}}");
    CHECK(!realtime_step(&rt,&client,"r1",7000) && rt.dirty_rooms);
    CHECK(!realtime_step(&rt,&client,"r2",8000) && !realtime_ready(&rt) && !strcmp(rt.subscription,"room-2"));
    ws_wire_frame(&wire,1,1,"{\"msg\":\"nosub\",\"id\":\"room-1\"}");
    ws_wire_frame(&wire,1,1,"{\"msg\":\"ready\",\"subs\":[\"room-2\",\"delete-2\"]}");
    CHECK(!realtime_step(&rt,&client,"r2",9000) && realtime_ready(&rt));
    rt.dirty_messages=0;
    ws_wire_frame(&wire,1,1,"{\"msg\":\"changed\",\"collection\":\"stream-notify-room\",\"fields\":{\"eventName\":\"r2/deleteMessage\"}}");
    CHECK(!realtime_step(&rt,&client,"r2",9500) && rt.dirty_messages);
    ws_wire_frame(&wire,1,1,"{\"msg\":\"ping\",\"id\":\"p1\"}");
    CHECK(!realtime_step(&rt,&client,"r2",10000) && rt.ws.open);
    CHECK(realtime_step(&rt,&client,"r2",101000)<0 && rt.retry_at>101000 && !rt.phase);
    { ar_ms delay=rt.retry_at-101000; realtime_failed(&rt,102000); CHECK(rt.retry_at-102000>delay); }
    ws_wire_upgrade(&wire,1); CHECK(!realtime_start(&rt,&client,200000));
    ws_wire_frame(&wire,1,1,"{\"msg\":\"connected\"}"); realtime_step(&rt,&client,"",201000);
    ws_wire_frame(&wire,1,1,"{\"msg\":\"result\",\"id\":\"login\",\"error\":{\"error\":403}}");
    CHECK(realtime_step(&rt,&client,"",202000)<0 && !rt.phase);
    ws_wire_upgrade(&wire,1); CHECK(!realtime_start(&rt,&client,300000));
    CHECK(realtime_step(&rt,&client,"",316000)<0 && !rt.phase);
    buf_clear(&wire.in); buf_clear(&wire.out); wire.pos=0;
    buf_puts(&wire.in,"HTTP/1.1 503 Busy\r\nDate: Tue, 24 Nov 2026 09:00:00 GMT\r\nRetry-After: Tue, 24 Nov 2026 09:02:00 GMT\r\n\r\n");
    CHECK(realtime_start(&rt,&client,400000)<0 && rt.retry_at>=520000);
    ws_wire_upgrade(&wire,1); wire.read_tick=200; wire.ticks=0; rt.failures=0;
    CHECK(realtime_start(&rt,&client,500000)<0 && rt.retry_at>=500000+wire.ticks+5000);
    wire.read_tick=wire.ticks=0;
    realtime_close(&rt); ws_close(&ws); rc_cleanup(&client); buf_free(&wire.in); buf_free(&wire.out);
}

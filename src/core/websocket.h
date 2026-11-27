/* Bounded RFC 6455 client. No compression/extensions; text messages only. */
#ifndef AR_WEBSOCKET_H
#define AR_WEBSOCKET_H
#include "net.h"
#include "buf.h"
#include "url.h"
#define WS_HANDSHAKE_MS 10000
#define WS_LIMIT (128UL*1024)
typedef struct {
    Transport tr; const NetIface *net;
    Buf input,message; int open,fragment,status,retry_after;
    char err[128];
} WebSocket;
void ws_init(WebSocket *w);
void ws_close(WebSocket *w);
int ws_open(WebSocket *w,const NetIface *net,const ServerUrl *url,int verify);
int ws_text(WebSocket *w,const char *text);
/* 1 complete text message, 0 no message yet, -1 closed/error. Bounded work. */
int ws_read(WebSocket *w);
#endif

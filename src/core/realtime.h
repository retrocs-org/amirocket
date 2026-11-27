/* Rocket.Chat DDP event notifications. REST remains authoritative for deltas. */
#ifndef AR_REALTIME_H
#define AR_REALTIME_H
#include "websocket.h"
#include "rc_api.h"
typedef struct {
    WebSocket ws;
    int phase,ready_mask,dirty_messages,dirty_rooms,failures;
    ar_ms retry_at,deadline,last_receive,ready_since;
    char room[64],subscription[32],deletion[32]; unsigned long serial;
} RcRealtime;
void realtime_init(RcRealtime *r);
void realtime_close(RcRealtime *r);
/* Caller governs connection attempts and only calls start when retry_at is due. */
int realtime_start(RcRealtime *r,RcClient *client,ar_ms now);
/* Nonblocking when idle; processes at most 16 DDP messages. Never sends REST. */
int realtime_step(RcRealtime *r,RcClient *client,const char *active_room,ar_ms now);
int realtime_ready(const RcRealtime *r);
void realtime_failed(RcRealtime *r,ar_ms now);
#endif

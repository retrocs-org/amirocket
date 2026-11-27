/* Real socket/TLS driver used by test_websocket.py. */
#include "../src/core/realtime.h"
#include "../src/posix/net_posix.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
static ar_ms clock_ms(void) { struct timeval t; gettimeofday(&t,NULL); return (ar_ms)t.tv_sec*1000+t.tv_usec/1000; }
int main(int argc,char **argv)
{
    RcClient c; RcRealtime r; ar_ms start,now; int live=0,first=0,second=0,rooms=0,switched=0,was=0,ok=0;
    const char *room="first"; signal(SIGPIPE,SIG_IGN); if(argc<3 || rc_init(&c,net_posix(),argv[1],1)) return 2;
    rc_server_info(&c);
    strcpy(c.user_id,"U"); strcpy(c.token,"test-resume-token"); realtime_init(&r); start=clock_ms();
    if(realtime_start(&r,&c,start)) { ok=!strcmp(argv[2],"failure") && r.retry_at>start; goto done; }
    while((now=clock_ms())-start<8000) {
        int rc=realtime_step(&r,&c,room,now),ready=realtime_ready(&r);
        if(ready && !was) { live++; r.dirty_messages=r.dirty_rooms=0; }
        if(ready && r.dirty_messages) { if(switched) second=1; else first=1; r.dirty_messages=0; }
        if(ready && r.dirty_rooms) { rooms=1; r.dirty_rooms=0; }
        was=ready;
        if(first && !switched) { room="second"; switched=1; }
        if(rc<0) { ok=!strcmp(argv[2],"failure") ? r.retry_at>now : live==2 && first && second && rooms && r.retry_at>now; break; }
        usleep(10000);
    }
done:
    printf("live=%d first=%d second=%d rooms=%d fallback=%d\n",live,first,second,rooms,!r.phase);
    realtime_close(&r); rc_cleanup(&c); return ok?0:1;
}

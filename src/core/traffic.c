#include "traffic.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
void traffic_init(ArTraffic *t,unsigned long seed)
{ memset(t,0,sizeof(*t)); t->spacing_ms=1000; t->seed=seed?seed:1; }
ar_ms traffic_wait(const ArTraffic *t,ar_ms now)
{ ar_ms until=t->next>t->cooldown?t->next:t->cooldown; return until>now?until-now:0; }
void traffic_start(ArTraffic *t,ar_ms now)
{
    ar_ms minute=now/60000;
    if(t->window!=minute) { t->previous=t->window+1==minute?t->recent:0; t->recent=0; t->window=minute; }
    t->recent++; t->requests++; t->next=now+t->spacing_ms;
}
unsigned long traffic_rpm(const ArTraffic *t,ar_ms now)
{
    ar_ms minute=now/60000;
    if(minute==t->window) return t->recent+t->previous*(unsigned long)(60000-now%60000)/60000;
    if(minute==t->window+1) return t->recent*(unsigned long)(60000-now%60000)/60000;
    return 0;
}
void traffic_result(ArTraffic *t,ar_ms now,int status,int retry_seconds)
{
    int delay=0;
    t->last_status=status;
    if(status==0 || status>=400) t->errors++;
    if(status==429) t->limited++;
    if(status==0 || status==429 || status>=500) {
        if(t->failures<7) t->failures++;
        delay=5<<(t->failures-1); if(delay>300) delay=300;
        t->seed=t->seed*1664525UL+1013904223UL;
        delay+=(int)(t->seed%((unsigned long)delay/4+1));
        if(retry_seconds>delay) delay=retry_seconds;
        if(now+(ar_ms)delay*1000>t->cooldown) t->cooldown=now+(ar_ms)delay*1000;
    } else if(status>=200 && status<400) t->failures=0;
}
int traffic_retry_after(const char *value,ar_ms date)
{
    const char *p=value; unsigned long n=0; ar_ms target;
    if(!p || !*p) return 0;
    if(isdigit((unsigned char)*p)) {
        while(isdigit((unsigned char)*p)) { if(n<86400) n=n*10+(unsigned)(*p-'0'); p++; }
        while(*p==' ' || *p=='\t') p++;
        return *p?0:(n>86400?86400:(int)n);
    }
    target=http_date_to_ms(value);
    if(!date || target<=date) return 0;
    return target-date>86400000?86400:(int)((target-date+999)/1000);
}

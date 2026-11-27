/* Read-only release/soak driver. Credentials and control arrive through private files.
 * This tool never sends, edits, deletes, marks read or logs message contents. */
#include "../src/core/session.h"
#include "../src/core/realtime.h"
#include "../src/core/traffic.h"
#include "../src/core/json.h"
#include "../src/posix/net_posix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif
#define WATCHES 8
static volatile sig_atomic_t stop;
static void stop_handler(int sig) { (void)sig; stop=1; }
static ar_ms now(void) { const NetIface *n=net_posix(); return n->now_ms(n->ctx); }
typedef struct { ArTraffic traffic; int errors; } Gate;
static int before(void *ctx)
{
    Gate *g=ctx; ar_ms t=now(),wait=traffic_wait(&g->traffic,t);
    if(g->traffic.cooldown>t) return -1;
    if(wait>0) usleep((unsigned)wait*1000);
    traffic_start(&g->traffic,now()); return 0;
}
static void after(void *ctx,int status,int retry)
{ Gate *g=ctx; traffic_result(&g->traffic,now(),status,retry); if(status<200 || status>=400) g->errors++; }
static int read_json(const char *path,char *data,size_t capacity,JDoc *doc)
{
    FILE *f=fopen(path,"rb"); size_t n; if(!f) return -1;
    n=fread(data,1,capacity-1,f); if(ferror(f) || !feof(f)) { fclose(f); return -1; } fclose(f); data[n]=0;
    return json_parse(doc,data,n);
}
static long rss_kib(void)
{
#ifdef __APPLE__
    struct mach_task_basic_info info; mach_msg_type_number_t size=MACH_TASK_BASIC_INFO_COUNT;
    if(task_info(mach_task_self(),MACH_TASK_BASIC_INFO,(task_info_t)&info,&size)==KERN_SUCCESS) return info.resident_size/1024;
#else
    FILE *f=fopen("/proc/self/statm","r"); long pages,unused; if(f) { if(fscanf(f,"%ld %ld",&unused,&pages)==2) { fclose(f); return pages*sysconf(_SC_PAGESIZE)/1024; } fclose(f); }
#endif
    return -1;
}
static int sockets(void)
{ int i,n=0; struct stat st; for(i=0;i<256;i++) if(!fstat(i,&st) && S_ISSOCK(st.st_mode)) n++; return n; }
static int duplicates(const RcMsgList *list)
{ int i,j,n=0; for(i=0;i<list->count;i++) for(j=0;j<i;j++) if(!strcmp(list->items[i].id,list->items[j].id)) n++; return n; }
static void report(const char *path,RcSession *s,RcRealtime *r,RcMsgList *cache,Gate *g,
                   ar_ms start,int reconnects,int forced,int syncs,int message_events,int room_events,
                   int last_error,int complete,char ids[WATCHES][96],char texts[WATCHES][256],int watch_count)
{
    char temp[1024]; FILE *f; int i,j; ar_ms t=now();
    snprintf(temp,sizeof(temp),"%s.new",path); f=fopen(temp,"wb"); if(!f) return;
    fprintf(f,"{\"elapsed_ms\":%lld,\"live\":%d,\"phase\":%d,\"reconnects\":%d,\"forced_drops\":%d,\"syncs\":%d,\"message_events\":%d,\"room_events\":%d,\"requests\":%lu,\"rpm\":%lu,\"http_errors\":%d,\"last_error\":%d,\"messages\":%d,\"duplicates\":%d,\"rss_kib\":%ld,\"socket_fds\":%d,\"cursor\":%lld,\"complete\":%d,\"watches\":[",
        t-start,realtime_ready(r),r->phase,reconnects,forced,syncs,message_events,room_events,g->traffic.requests,traffic_rpm(&g->traffic,t),g->errors,last_error,cache->count,duplicates(cache),rss_kib(),sockets(),s->active_last,complete);
    for(i=0;i<watch_count;i++) {
        int present=0,match=0,count=0; Buf id; buf_init(&id); json_put_string(&id,ids[i]);
        for(j=0;j<cache->count;j++) if(!strcmp(cache->items[j].id,ids[i])) { present=1; count++; match=cache->items[j].text && !strcmp(cache->items[j].text,texts[i]); }
        fprintf(f,"%s{\"id\":%s,\"present\":%d,\"text_matches\":%d,\"count\":%d}",i?",":"",buf_str(&id),present,match,count); buf_free(&id);
    }
    { Buf error; buf_init(&error); json_put_string(&error,r->ws.err); fprintf(f,"],\"websocket_status\":%d,\"websocket_error\":%s,\"websocket_failures\":%d}\n",r->ws.status,buf_str(&error),r->failures); buf_free(&error); } if(fclose(f)==0) rename(temp,path);
}
int main(int argc,char **argv)
{
    RcSession s; RcRealtime r; RcMsgList cache; Gate g={0}; JDoc d; char data[8192],url[512],uid[64],token[128],rid[64];
    char ids[WATCHES][96]={{0}},texts[WATCHES][256]={{0}};
    ar_ms start,end,report_at=0,poll_at=0,rooms_at=0,sync_at=0,pause_until=0,control_at=0; long seq=0;
    int duration,reconnects=0,forced=0,syncs=0,message_events=0,room_events=0,last_error=0,watch_count=0,ready_before=0,failed=0;
    if(argc!=5) { fprintf(stderr,"usage: realtime_check CREDENTIALS.json SECONDS REPORT.json CONTROL.json\n"); return 2; }
    umask(077); signal(SIGPIPE,SIG_IGN); signal(SIGTERM,stop_handler); signal(SIGINT,stop_handler); duration=atoi(argv[2]); if(duration<1 || duration>86400) return 2;
    if(read_json(argv[1],data,sizeof(data),&d)) return 2;
    json_str(&d,json_get(&d,0,"url"),url,sizeof(url)); json_str(&d,json_get(&d,0,"user_id"),uid,sizeof(uid));
    json_str(&d,json_get(&d,0,"token"),token,sizeof(token)); json_str(&d,json_get(&d,0,"room_id"),rid,sizeof(rid)); json_free(&d); memset(data,0,sizeof(data));
    if(!url[0] || !uid[0] || !token[0] || !rid[0] || sess_init(&s,net_posix(),url,1)) return 2;
    traffic_init(&g.traffic,321); s.rc.http.before_request=before; s.rc.http.after_request=after; s.rc.http.scheduler_ctx=&g;
    s.foreground=0; realtime_init(&r); rc_msglist_init(&cache); start=now(); end=start+(ar_ms)duration*1000;
    last_error=sess_login(&s,NULL,NULL,NULL,uid,token); memset(token,0,sizeof(token));
    if(last_error || (last_error=sess_open_room(&s,rid,&cache))) { failed=1; goto done; }
    while(!stop && now()<end) {
        ar_ms t=now(); int live;
        if(t>=control_at) {
            control_at=t+250;
            if(!read_json(argv[4],data,sizeof(data),&d)) {
                long next=(long)json_ll(&d,json_get(&d,0,"seq"),0);
                if(next>seq) {
                    int a=json_get(&d,0,"watches"),i; seq=next; watch_count=json_size(&d,a); if(watch_count>WATCHES) watch_count=WATCHES; if(watch_count<0) watch_count=0;
                    for(i=0;i<watch_count;i++) { int o=json_at(&d,a,i); json_str(&d,json_get(&d,o,"id"),ids[i],sizeof(ids[i])); json_str(&d,json_get(&d,o,"text"),texts[i],sizeof(texts[i])); }
                    { int pause=(int)json_ll(&d,json_get(&d,0,"pause_seconds"),0); if(pause>0 && pause<=120) { realtime_failed(&r,t); pause_until=t+(ar_ms)pause*1000; forced++; poll_at=t; ready_before=0; } }
                    if(json_bool(&d,json_get(&d,0,"stop"),0)) stop=1;
                }
                json_free(&d);
            }
        }
        if(!r.phase && t>=pause_until && t>=r.retry_at && !traffic_wait(&g.traffic,t)) {
            traffic_start(&g.traffic,t);
            if(realtime_start(&r,&s.rc,t) && (r.ws.status==429 || r.ws.status==503)) traffic_result(&g.traffic,now(),r.ws.status,r.ws.retry_after);
        }
        if(r.phase && realtime_step(&r,&s.rc,s.active_rid,t)) poll_at=t;
        live=realtime_ready(&r);
        if(live && !ready_before) { reconnects++; poll_at=t; }
        else if(live) { if(r.dirty_messages) message_events++; if(r.dirty_rooms) room_events++; }
        ready_before=live;
        if((r.dirty_messages || r.dirty_rooms) && t>=sync_at && poll_at>t) poll_at=t;
        if(t>=poll_at && t>=g.traffic.cooldown) {
            RcRoomList rooms; RcMsgList delta; int fetch_rooms=t>=rooms_at || r.dirty_rooms;
            rc_roomlist_init(&rooms); rc_msglist_init(&delta);
            last_error=sess_poll(&s,fetch_rooms?&rooms:NULL,&delta); syncs++;
            if(!last_error) {
                rc_msglist_merge(&cache,&delta,200); r.dirty_messages=0; if(fetch_rooms) { r.dirty_rooms=0; rooms_at=t+(live?120000:30000); }
                sync_at=now()+2000;
            } else if(last_error==RC_ERR_AUTH) failed=1;
            rc_roomlist_free(&rooms); rc_msglist_free(&delta);
            poll_at=now()+(live?60000:5000);
        }
        if(duplicates(&cache)) failed=1;
        if(t>=report_at) { report(argv[3],&s,&r,&cache,&g,start,reconnects,forced,syncs,message_events,room_events,last_error,0,ids,texts,watch_count); report_at=t+250; }
        if(failed) break;
        usleep(50000);
    }
done:
    report(argv[3],&s,&r,&cache,&g,start,reconnects,forced,syncs,message_events,room_events,last_error,!failed,ids,texts,watch_count);
    printf("Realtime check: %s; connections=%d forced-drops=%d syncs=%d errors=%d\n",failed?"FAIL":"PASS",reconnects,forced,syncs,g.errors);
    realtime_close(&r); rc_msglist_free(&cache); sess_cleanup(&s); return failed?1:0;
}

/* Two real, simultaneous sessions against independent mock Rocket.Chat servers. */
#include "../src/core/session.h"
#include "../src/core/mem.h"
#include "../src/core/json.h"
#include "../src/posix/net_posix.h"
#include <stdio.h>
#include <string.h>
static int contains(const RcMsgList *list,const char *text)
{
    int i; for(i=0;i<list->count;i++) if(list->items[i].text && strstr(list->items[i].text,text)) return 1; return 0;
}
static int read_count(RcSession *s)
{
    HttpResponse response; JDoc doc; int count=-1;
    http_response_init(&response); memset(&doc,0,sizeof(doc));
    if(!http_request(&s->rc.http,"POST","/mock/read-calls",NULL,"{}",2,&response) &&
       !json_parse(&doc,buf_str(&response.body),response.body.len)) count=json_size(&doc,json_get(&doc,0,"calls"));
    json_free(&doc); http_response_free(&response); return count;
}
#define CHECK(step,condition) do { if(!(condition)) { fprintf(stderr,"multi-session failed: %s\n",step); goto done; } } while(0)
int main(int argc,char **argv)
{
    RcSession s[2]; RcMsgList msgs[2]; RcRoomList rooms; int i,rc=20,reads;
    memset(s,0,sizeof(s)); for(i=0;i<2;i++) rc_msglist_init(&msgs[i]); rc_roomlist_init(&rooms);
    if(argc!=3 || ar_mem_init()<0) return 20;
    CHECK("first init",sess_init(&s[0],net_posix(),argv[1],1)==RC_OK);
    CHECK("second init",sess_init(&s[1],net_posix(),argv[2],1)==RC_OK);
    for(i=0;i<2;i++) CHECK("login",sess_login(&s[i],"tommy","secret",NULL,NULL,NULL)==RC_OK);
    CHECK("independent tokens",strcmp(s[0].rc.token,s[1].rc.token));
    for(i=0;i<2;i++) CHECK("open same room id",sess_open_room(&s[i],"general",&msgs[i])==RC_OK);
    CHECK("same room identifiers",!strcmp(s[0].active_rid,s[1].active_rid));
    rc_msglist_free(&msgs[0]); rc_msglist_free(&msgs[1]);
    CHECK("send first",rc_send(&s[0].rc,s[0].active_rid,"isolated-first-server",&msgs[0])==RC_OK);
    CHECK("first response",contains(&msgs[0],"isolated-first-server"));
    CHECK("poll second",sess_poll(&s[1],&rooms,&msgs[1])==RC_OK);
    CHECK("no first message on second",!contains(&msgs[1],"isolated-first-server"));
    rc_msglist_free(&msgs[0]); rc_msglist_free(&msgs[1]); rc_roomlist_free(&rooms);
    CHECK("send second",rc_send(&s[1].rc,s[1].active_rid,"isolated-second-server",&msgs[1])==RC_OK);
    CHECK("second response",contains(&msgs[1],"isolated-second-server"));
    reads=read_count(&s[0]); CHECK("read counter",reads>=0);
    s[0].foreground=0;
    CHECK("poll first",sess_poll(&s[0],&rooms,&msgs[0])==RC_OK);
    CHECK("background does not mark messages read",read_count(&s[0])==reads);
    CHECK("no second message on first",!contains(&msgs[0],"isolated-second-server"));
    CHECK("logout first",rc_logout(&s[0].rc)==RC_OK);
    CHECK("second survives logout",sess_poll(&s[1],&rooms,&msgs[1])==RC_OK);
    puts("multi-session isolation passed"); rc=0;
done:
    for(i=0;i<2;i++) { rc_msglist_free(&msgs[i]); sess_cleanup(&s[i]); }
    rc_roomlist_free(&rooms); ar_mem_cleanup(); return rc;
}

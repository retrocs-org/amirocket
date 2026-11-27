/* Native checks for shared resources with the executable in a different drawer. */
#include "../src/amiga/prefs.h"
#include "../src/amiga/plugins.h"
#include "../src/core/mem.h"
#include <dos/dos.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
static int passed,failed;
#define QA(x) do {if(x)passed++;else {failed++;printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
int main(void)
{
    Prefs *p;char path[512];BPTR lock;ArPluginHost host={0};
    if(ar_mem_init()<0)return 20;
    p=ar_malloc(sizeof(*p));if(!p)return 20;prefs_defaults(p);
    QA(prefs_resource_path(p,"PROGDIR:Plugins/timestamp.arp",path,sizeof(path))==0 && !strcmp(path,"PROGDIR:Plugins/timestamp.arp"));
    QA(prefs_set(p,"workspace.data_dir","SharedData:")==0);
    QA(prefs_resource_path(p,"PROGDIR:Plugins/timestamp.arp",path,sizeof(path))==0 && !strcmp(path,"SharedData:Plugins/timestamp.arp"));
    lock=Lock(path,ACCESS_READ);QA(lock!=0);if(lock)UnLock(lock);
    QA(prefs_resource_path(p,"PROGDIR:Sounds/Notification.8svx",path,sizeof(path))==0 && !strcmp(path,"SharedData:Sounds/Notification.8svx"));
    lock=Lock(path,ACCESS_READ);QA(lock!=0);if(lock)UnLock(lock);
    QA(prefs_resource_path(p,"T:custom.8svx",path,sizeof(path))==0 && !strcmp(path,"T:custom.8svx"));
    QA(prefs_resource_path(p,"PROGDIR:Plugins/timestamp.arp",path,8)<0);
    QA(prefs_resource_path(p,"PROGDIR:AmiRocket.drafts",path,sizeof(path))==0 && !strcmp(path,"SharedData:AmiRocket.drafts"));
    QA(prefs_set(p,"plugin.timestamp.path","PROGDIR:Plugins/timestamp.arp")==0);
    QA(prefs_set(p,"plugin.timestamp.enabled","1")==0);
    plugins_init(&host);plugins_sync(p);QA(plugins_count()==1 && !strcmp(plugins_id(0),"timestamp"));plugins_stop();
    memset(p,0,sizeof(*p));ar_free(p);ar_mem_cleanup();
    printf("Shared-resource checks: %d passed, %d failed\n",passed,failed);return failed?5:0;
}

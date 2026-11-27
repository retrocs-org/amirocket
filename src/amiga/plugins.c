#include "plugins.h"
#include <dos/dos.h>
#include <proto/dos.h>
#include <string.h>
#include <stdio.h>
#define PLUGIN_LIMIT 8
static struct { BPTR segment; const ArPlugin *plugin; ArPluginHost host; char id[32], path[256]; } slots[PLUGIN_LIMIT];
static ArPluginHost callbacks;
static int count;
static char error[256];
static int valid_id(const char *id)
{
    const char *p; if (!id || !*id || strlen(id) > 31) return 0;
    for (p=id;*p;p++) if (!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_')) return 0;
    return 1;
}
void plugins_init(const ArPluginHost *host) { callbacks = *host; }
void plugins_stop(void)
{
    int i; for (i=count-1;i>=0;i--) {
        if (slots[i].plugin->stop) slots[i].plugin->stop(&slots[i].host);
        UnLoadSeg(slots[i].segment);
    }
    memset(slots,0,sizeof(slots)); count=0;
}
int plugins_sync(const Prefs *p)
{
    char ids[PLUGIN_LIMIT][32], paths[PLUGIN_LIMIT][256]; int n=0,i,changed=0;
    error[0]=0;
    for (i=0;i<p->extra_count && n<PLUGIN_LIMIT;i++) {
        const char *key=p->extra[i].key; size_t len=strlen(key); char enabled[80];
        if (len <= 12 || strncmp(key,"plugin.",7) || strcmp(key+len-5,".path")) continue;
        len-=12; if (len>=32) continue;
        memcpy(ids[n],key+7,len); ids[n][len]=0; if (!valid_id(ids[n])) continue;
        snprintf(enabled,sizeof(enabled),"plugin.%s.enabled",ids[n]);
        if (strcmp(prefs_extra(p,enabled,"0"),"1")) continue;
        if (!p->extra[i].value[0]) continue;
        if(prefs_resource_path(p,p->extra[i].value,paths[n],sizeof(paths[n]))<0)continue;
        n++;
    }
    if (n!=count) changed=1;
    for (i=0;i<n && !changed;i++) if (strcmp(ids[i],slots[i].id)||strcmp(paths[i],slots[i].path)) changed=1;
    if (!changed) return 0;
    plugins_stop();
    for (i=0;i<n;i++) {
        BPTR segment=LoadSeg(paths[i]); const ArPluginFile *file; const ArPlugin *plugin;
        if (!segment) { snprintf(error,sizeof(error),"Cannot load plugin %s",ids[i]); continue; }
        file=(const ArPluginFile *)((unsigned char *)BADDR(segment)+4);
        if (file->magic!=AR_PLUGIN_MAGIC || file->abi!=AR_PLUGIN_ABI || file->size<sizeof(*file) || !file->entry) {
            snprintf(error,sizeof(error),"Plugin %s has an incompatible header",ids[i]); UnLoadSeg(segment); continue;
        }
        plugin=file->entry();
        if (!plugin || plugin->abi!=AR_PLUGIN_ABI || plugin->size<sizeof(*plugin) || !plugin->id || strcmp(plugin->id,ids[i]) || !plugin->action || !plugin->action_label) {
            snprintf(error,sizeof(error),"Plugin %s has an incompatible ABI or id",ids[i]); UnLoadSeg(segment); continue;
        }
        snprintf(slots[count].id,sizeof(slots[count].id),"%s",ids[i]);
        snprintf(slots[count].path,sizeof(slots[count].path),"%s",paths[i]);
        slots[count].host=callbacks; slots[count].host.context=slots[count].id;
        if (plugin->start && !plugin->start(&slots[count].host)) { UnLoadSeg(segment); memset(&slots[count],0,sizeof(slots[count])); continue; }
        slots[count].segment=segment; slots[count].plugin=plugin; count++;
    }
    return 1;
}
int plugins_count(void) { return count; }
const char *plugins_id(int i) { return i>=0&&i<count ? slots[i].id : ""; }
const char *plugins_label(int i) { return i>=0&&i<count ? slots[i].plugin->action_label : ""; }
const char *plugins_error(void) { return error; }
void plugins_action(int i) { if(i>=0&&i<count) slots[i].plugin->action(&slots[i].host); }
void plugins_event(const char *kind,const char *room,const char *user,const char *text)
{
    ArPluginEvent event={sizeof(ArPluginEvent),kind,room?room:"",user?user:"",text?text:""}; int i;
    for(i=0;i<count;i++) if(slots[i].plugin->event) slots[i].plugin->event(&slots[i].host,&event);
}

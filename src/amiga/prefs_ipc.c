#include "prefs_ipc.h"
#include <exec/execbase.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_MAGIC 0x41524331UL
#define CONFIG_PREFIX "AmiRocket.Config.v4."
struct PrefsEndpoint { struct MsgPort port; ULONG magic; char name[64]; Prefs live; char plugins[272]; };
static int matches(struct Node *node)
{
    return node->ln_Name && !strncmp(node->ln_Name,CONFIG_PREFIX,sizeof(CONFIG_PREFIX)-1) &&
        ((PrefsEndpoint *)node)->magic == CONFIG_MAGIC;
}
PrefsEndpoint *prefs_service_start(const Prefs *p)
{
    PrefsEndpoint *s = AllocVec(sizeof(*s),MEMF_PUBLIC|MEMF_CLEAR); BYTE bit;
    if (!s) return NULL;
    bit = AllocSignal(-1); if (bit < 0) { FreeVec(s); return NULL; }
    s->magic = CONFIG_MAGIC; s->live = *p;
    snprintf(s->name,sizeof(s->name),CONFIG_PREFIX "%lx",(unsigned long)FindTask(NULL));
    s->port.mp_Node.ln_Name = s->name; s->port.mp_Node.ln_Type = NT_MSGPORT;
    s->port.mp_Flags = PA_SIGNAL; s->port.mp_SigTask = FindTask(NULL); s->port.mp_SigBit = bit;
    NewList(&s->port.mp_MsgList); AddPort(&s->port); return s;
}
void prefs_update_free(PrefsUpdate *m) { FreeVec(m); }
void prefs_service_stop(PrefsEndpoint *s)
{
    struct Message *m; if (!s) return;
    RemPort(&s->port); while ((m = GetMsg(&s->port))) FreeVec(m);
    FreeSignal(s->port.mp_SigBit); FreeVec(s);
}
ULONG prefs_service_signal(PrefsEndpoint *s) { return s ? 1UL << s->port.mp_SigBit : 0; }
PrefsUpdate *prefs_service_next(PrefsEndpoint *s)
{
    PrefsUpdate *m;
    if (!s) return NULL;
    while ((m = (PrefsUpdate *)GetMsg(&s->port))) {
        if (m->msg.mn_Length == sizeof(*m) && m->magic == CONFIG_MAGIC) return m;
        FreeVec(m);
    }
    return NULL;
}
void prefs_service_publish(PrefsEndpoint *s,const Prefs *p)
{
    if (s) { Forbid(); s->live = *p; Permit(); }
}
int prefs_runtime_get(Prefs *out)
{
    struct Node *n; int found = 0;
    Forbid();
    for (n = SysBase->PortList.lh_Head; n->ln_Succ; n = n->ln_Succ) if (matches(n)) { *out = ((PrefsEndpoint *)n)->live; found = 1; break; }
    Permit(); return found;
}
static int prefs_runtime_send_internal(const Prefs *p,int slot,const ServerPrefs *server,int permanent)
{
    PrefsUpdate *messages[8]; struct Node *n; int i, sent = 0;
    for (i = 0; i < 8; i++) {
        messages[i] = AllocVec(sizeof(PrefsUpdate),MEMF_PUBLIC|MEMF_CLEAR);
        if (!messages[i]) break;
        messages[i]->msg.mn_Length = sizeof(PrefsUpdate); messages[i]->magic = CONFIG_MAGIC;
        messages[i]->permanent = permanent;
        if(server) { messages[i]->server_only=slot+1; messages[i]->prefs.servers[slot]=*server; }
        else messages[i]->prefs = *p;
    }
    Forbid();
    for (n = SysBase->PortList.lh_Head; n->ln_Succ && sent < i; n = n->ln_Succ)
        if (matches(n)) PutMsg((struct MsgPort *)n,(struct Message *)messages[sent++]);
    Permit();
    while (i > sent) FreeVec(messages[--i]);
    return sent;
}

void prefs_service_plugins(PrefsEndpoint *s,const char *ids)
{
    if(s) { Forbid(); snprintf(s->plugins,sizeof(s->plugins),"%s",ids); Permit(); }
}
int prefs_runtime_plugins(char *out,int size)
{
    struct Node *n; int found=0;
    if(size<=0) return 0;
    out[0]=0; Forbid();
    for(n=SysBase->PortList.lh_Head;n->ln_Succ;n=n->ln_Succ) if(matches(n)) {
        snprintf(out,size,"%s",((PrefsEndpoint *)n)->plugins); found=1; break;
    }
    Permit(); return found;
}

int prefs_runtime_send(const Prefs *p,int permanent) { return prefs_runtime_send_internal(p,0,NULL,permanent); }
int prefs_runtime_send_server(int slot,const ServerPrefs *server,int permanent)
{
    if(slot<0 || slot>=PREFS_SERVERS_MAX || !server) return 0;
    return prefs_runtime_send_internal(NULL,slot,server,permanent);
}

#include "amirocket_plugin.h"
static void insert_reply(const ArPluginHost *host)
{
    char text[256];
    if (host->config_get(host->context,"text",text,sizeof(text)) < 0)
        host->insert_text(host->context,"Hello from AmiRocket!");
    else host->insert_text(host->context,text);
}
static const ArPlugin plugin = { AR_PLUGIN_ABI, sizeof(ArPlugin), "quickreply", "Quick reply", "Insert quick reply", 0, 0, insert_reply, 0 };
const ArPlugin *ar_plugin_entry(void) { return &plugin; }

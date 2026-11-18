#include "amirocket_plugin.h"
static void insert_time(const ArPluginHost *host)
{
    char time[32];
    host->local_time(host->context,time,sizeof(time));
    host->insert_text(host->context,time);
}
static const ArPlugin plugin = { AR_PLUGIN_ABI, sizeof(ArPlugin), "timestamp", "Timestamp", "Insert timestamp", 0, 0, insert_time, 0 };
const ArPlugin *ar_plugin_entry(void) { return &plugin; }

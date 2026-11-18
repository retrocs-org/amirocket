#ifndef AR_PREFS_IPC_H
#define AR_PREFS_IPC_H
#include "prefs.h"
#include <exec/ports.h>
typedef struct PrefsEndpoint PrefsEndpoint;
typedef struct { struct Message msg; ULONG magic; int permanent,server_only; Prefs prefs; } PrefsUpdate;
PrefsEndpoint *prefs_service_start(const Prefs *prefs);
void prefs_service_stop(PrefsEndpoint *service);
ULONG prefs_service_signal(PrefsEndpoint *service);
PrefsUpdate *prefs_service_next(PrefsEndpoint *service);
void prefs_service_publish(PrefsEndpoint *service, const Prefs *prefs);
void prefs_update_free(PrefsUpdate *update);
void prefs_service_plugins(PrefsEndpoint *service, const char *ids);
int prefs_runtime_plugins(char *out, int size);
int prefs_runtime_get(Prefs *out); /* effective settings of the first running client */
int prefs_runtime_send_server(int slot,const ServerPrefs *server,int permanent);
int prefs_runtime_send(const Prefs *prefs, int permanent); /* broadcasts, transfers ownership */
#endif

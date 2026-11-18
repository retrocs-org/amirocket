#ifndef AR_PLUGINS_H
#define AR_PLUGINS_H
#include "prefs.h"
#include "../../sdk/include/amirocket_plugin.h"
void plugins_init(const ArPluginHost *host);
int plugins_sync(const Prefs *prefs); /* enabled modules; does not run arbitrary directory contents */
void plugins_stop(void);
int plugins_count(void);
const char *plugins_id(int index);
const char *plugins_label(int index);
const char *plugins_error(void);
void plugins_action(int index);
void plugins_event(const char *kind,const char *room,const char *user,const char *text);
#endif

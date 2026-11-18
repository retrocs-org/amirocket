#ifndef AR_AREXX_H
#define AR_AREXX_H
#include <exec/types.h>
struct RexxMsg;
int arexx_is_packet(const struct RexxMsg *message);
typedef int (*ArRexxCommand)(const char *command,char *result,int size);
int arexx_start(ArRexxCommand command);
void arexx_stop(void);
ULONG arexx_signal(void);
const char *arexx_port_name(void);
void arexx_handle(void);
int arexx_subscribe(const char *port);
int arexx_subscribe_version(const char *port,int version);
const char *arexx_epoch(void);
ULONG arexx_sequence(void);
ULONG arexx_dropped(void);
int arexx_pending(void);
int arexx_unsubscribe(const char *port);
void arexx_event(const char *kind,const char *room,const char *user,const char *text);
#endif

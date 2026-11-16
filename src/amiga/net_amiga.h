/*
 * AmiRocket - bsdsocket.library + AmiSSL transport.
 *
 * Library bases for bsdsocket and AmiSSL belong to the task that opened
 * them. Call net_amiga_open()/net_amiga_close() from the task that does the
 * networking (the worker process in the GUI, the main task in PROBE mode).
 */
#ifndef AR_NET_AMIGA_H
#define AR_NET_AMIGA_H

#include "../core/net.h"

int  net_amiga_open(char *err, int errsz);   /* 0 ok */
void net_amiga_close(void);
const NetIface *net_amiga(void);

/* AmiSSL is opened lazily on the first https connection. */
int  net_amiga_tls_available(char *err, int errsz);
const char *net_amiga_tls_version(void);

#endif

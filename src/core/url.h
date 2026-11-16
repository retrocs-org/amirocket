/* AmiRocket - server URL parsing and query encoding. */
#ifndef AR_URL_H
#define AR_URL_H

#include "buf.h"

typedef struct {
    int  tls;            /* 1 for https */
    char host[256];
    int  port;
    char base[256];      /* path prefix without trailing slash, "" for root */
} ServerUrl;

/* Accepts "chat.example.com", "https://chat.example.com:3000/rc/".
 * A bare host name defaults to https. Returns 0 or -1 with err filled. */
int url_parse_server(const char *in, ServerUrl *out, char *err, int errsz);

/* Appends a percent-encoded query component. */
void url_encode(Buf *b, const char *s);

#endif

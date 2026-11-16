#include "url.h"
#include "util.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int url_parse_server(const char *in, ServerUrl *out, char *err, int errsz)
{
    const char *p = in, *host, *hend, *path;
    size_t hl;

    memset(out, 0, sizeof(*out));
    while (*p == ' ' || *p == '\t')
        p++;
    out->tls = 1;
    if (ar_strncasecmp(p, "https://", 8) == 0) {
        p += 8;
    } else if (ar_strncasecmp(p, "http://", 7) == 0) {
        out->tls = 0;
        p += 7;
    } else if (strstr(p, "://")) {
        snprintf(err, (size_t)errsz, "Unsupported URL scheme");
        return -1;
    }
    host = p;
    while (*p && *p != ':' && *p != '/' && *p != ' ' && *p != '?')
        p++;
    hend = p;
    hl = (size_t)(hend - host);
    if (hl == 0 || hl >= sizeof(out->host)) {
        snprintf(err, (size_t)errsz, "Missing or invalid host name");
        return -1;
    }
    memcpy(out->host, host, hl);
    out->host[hl] = 0;
    out->port = out->tls ? 443 : 80;
    if (*p == ':') {
        long port;
        char *e;
        p++;
        port = strtol(p, &e, 10);
        if (e == p || port <= 0 || port > 65535) {
            snprintf(err, (size_t)errsz, "Invalid port number");
            return -1;
        }
        out->port = (int)port;
        p = e;
    }
    path = p;
    if (*path == '/') {
        size_t pl = strcspn(path, " ?#");
        while (pl > 0 && path[pl - 1] == '/')
            pl--;
        if (pl >= sizeof(out->base)) {
            snprintf(err, (size_t)errsz, "Path too long");
            return -1;
        }
        memcpy(out->base, path, pl);
        out->base[pl] = 0;
    }
    return 0;
}

void url_encode(Buf *b, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";

    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            buf_putc(b, (char)c);
        } else {
            char e[3];
            e[0] = '%';
            e[1] = hex[c >> 4];
            e[2] = hex[c & 15];
            buf_append(b, e, 3);
        }
    }
}

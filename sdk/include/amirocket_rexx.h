#ifndef AMIROCKET_REXX_H
#define AMIROCKET_REXX_H
/* Header-only helpers for native dock clients. Text is data, never executable. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define AR_REXX_PROTOCOL 2
#define AR_REXX_POLL_SECONDS 10

typedef struct {
    char epoch[80],kind[32];
    unsigned long sequence,dropped;
    int truncated;
} ArDockEvent;

/* Reads one key=value token from a snapshot header (before the first newline). */
static inline int ar_rexx_field(const char *line,const char *key,char *out,size_t size)
{
    size_t n=strlen(key);
    if(!out || !size) return 0;
    out[0]=0;
    while(line && *line && *line!='\n' && *line!='\r') {
        const char *end; size_t len;
        while(*line==' ' || *line=='\t') line++;
        end=line; while(*end && *end!=' ' && *end!='\t' && *end!='\n' && *end!='\r') end++;
        len=(size_t)(end-line);
        if(len>n && !strncmp(line,key,n) && line[n]=='=') {
            len-=n+1; if(len>=size) return 0;
            memcpy(out,line+n+1,len); out[len]=0; return 1;
        }
        line=end;
    }
    return 0;
}
static inline int ar_rexx_number(const char *line,const char *key,unsigned long *out)
{
    char value[32],*end; unsigned long n;
    if(!ar_rexx_field(line,key,value,sizeof(value)) || value[0]<'0' || value[0]>'9') return 0;
    errno=0; n=strtoul(value,&end,10);
    if(*end || errno==ERANGE) return 0;
    *out=n; return 1;
}
/* Skips a quoted ARexx field; a doubled quote represents one literal quote. */
static inline const char *ar_rexx_skip_quoted(const char *p)
{
    while(*p==' ') p++;
    if(*p++!='"') return NULL;
    while(*p) {
        if(*p=='"') { if(p[1]=='"') p+=2; else return p+1; }
        else p++;
    }
    return NULL;
}
static inline int ar_rexx_event2(const char *command,ArDockEvent *out)
{
    const char *p; int used=0,i; unsigned long truncated;
    memset(out,0,sizeof(*out));
    if(strncmp(command,"AMIROCKET_EVENT2 ",17) ||
       sscanf(command,"AMIROCKET_EVENT2 %79s %lu %31s %n",out->epoch,&out->sequence,out->kind,&used)!=3 || !used) return 0;
    p=command+used;
    for(i=0;i<3;i++) { p=ar_rexx_skip_quoted(p); if(!p) return 0; }
    if(!ar_rexx_number(p,"dropped",&out->dropped) || !ar_rexx_number(p,"truncated",&truncated) || truncated>1) return 0;
    out->truncated=(int)truncated; return 1;
}
#endif

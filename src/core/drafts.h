#ifndef AR_DRAFTS_H
#define AR_DRAFTS_H
#include "buf.h"
#define AR_DRAFT_COUNT 64
#define AR_DRAFT_TEXT 8000
#define AR_DRAFT_BYTES (1024UL*1024)
typedef struct { char *key,*text; int cursor,dirty; } ArDraft;
typedef struct { ArDraft entries[AR_DRAFT_COUNT]; } ArDrafts;
void drafts_free(ArDrafts *d);
const ArDraft *drafts_get(const ArDrafts *d,const char *key);
int drafts_set(ArDrafts *d,const char *key,const char *text,int cursor);
int drafts_pending(ArDrafts *d,const char *key,const char *text,int cursor);
void drafts_commit(ArDrafts *d,const char *key);
int drafts_decode(ArDrafts *d,const char *data,size_t length);
int drafts_encode(const ArDrafts *d,Buf *out);
#endif

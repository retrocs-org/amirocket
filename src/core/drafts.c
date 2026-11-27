#include "drafts.h"
#include "json.h"
#include "mem.h"
#include "util.h"
#include <string.h>
void drafts_free(ArDrafts *d) { int i; for(i=0;i<AR_DRAFT_COUNT;i++) { ar_free(d->entries[i].key); ar_free(d->entries[i].text); } memset(d,0,sizeof(*d)); }
const ArDraft *drafts_get(const ArDrafts *d,const char *key) { int i; for(i=0;i<AR_DRAFT_COUNT;i++) if(d->entries[i].key && !strcmp(d->entries[i].key,key)) return &d->entries[i]; return NULL; }
int drafts_set(ArDrafts *d,const char *key,const char *text,int cursor)
{
    int i,empty=-1; char *k,*t;
    if(!key || !*key || strlen(key)>511 || !text || strlen(text)>AR_DRAFT_TEXT || cursor<0 || cursor>4000) return -1;
    for(i=0;i<AR_DRAFT_COUNT;i++) { if(!d->entries[i].key) { if(empty<0) empty=i; } else if(!strcmp(d->entries[i].key,key)) break; }
    if(i==AR_DRAFT_COUNT) { if(!*text) return 0; i=empty; }
    if(i<0) return -1;
    if(!*text) { ar_free(d->entries[i].key); ar_free(d->entries[i].text); memset(&d->entries[i],0,sizeof(ArDraft)); return 0; }
    k=ar_strdup(key); t=ar_strdup(text); if(!k || !t) { ar_free(k); ar_free(t); return -1; }
    ar_free(d->entries[i].key); ar_free(d->entries[i].text); d->entries[i].key=k; d->entries[i].text=t; d->entries[i].cursor=cursor; d->entries[i].dirty=0; return 0;
}
int drafts_pending(ArDrafts *d,const char *key,const char *text,int cursor)
{
    ArDraft *entry; int i;
    if(drafts_set(d,key,text,cursor)<0) return -1;
    entry=(ArDraft *)drafts_get(d,key);
    if(!entry) {
        for(i=0;i<AR_DRAFT_COUNT && d->entries[i].key;i++);
        if(i==AR_DRAFT_COUNT) return 0; /* no existing value to delete */
        entry=&d->entries[i]; entry->key=ar_strdup(key); entry->text=ar_strdup("");
        if(!entry->key || !entry->text) { ar_free(entry->key); ar_free(entry->text); memset(entry,0,sizeof(*entry)); return -1; }
    }
    entry->dirty=1; return 0;
}
void drafts_commit(ArDrafts *d,const char *key)
{
    ArDraft *entry=(ArDraft *)drafts_get(d,key);
    if(!entry) return;
    if(!entry->text[0]) drafts_set(d,key,"",0); else entry->dirty=0;
}
int drafts_encode(const ArDrafts *d,Buf *out)
{
    int i,n=0; buf_clear(out); buf_puts(out,"{\"version\":1,\"drafts\":[");
    for(i=0;i<AR_DRAFT_COUNT;i++) if(d->entries[i].key && d->entries[i].text[0]) {
        if(n++) buf_putc(out,',');
        buf_puts(out,"{\"key\":"); json_put_string(out,d->entries[i].key);
        buf_puts(out,",\"text\":"); json_put_string(out,d->entries[i].text); buf_printf(out,",\"cursor\":%d}",d->entries[i].cursor);
        if(out->oom || out->len>AR_DRAFT_BYTES) return -1;
    }
    buf_puts(out,"]}"); return out->oom || out->len>AR_DRAFT_BYTES?-1:0;
}
int drafts_decode(ArDrafts *d,const char *data,size_t length)
{
    JDoc doc; ArDrafts fresh={0}; int arr,i,rc=-1;
    memset(&doc,0,sizeof(doc));
    if(length>AR_DRAFT_BYTES || json_parse(&doc,data,length)<0) goto done;
    if(json_ll(&doc,json_get(&doc,0,"version"),0)!=1) goto done;
    arr=json_get(&doc,0,"drafts"); if(json_type(&doc,arr)!=JT_ARRAY || json_size(&doc,arr)>AR_DRAFT_COUNT) goto done;
    for(i=0;i<json_size(&doc,arr);i++) {
        int item=json_at(&doc,arr,i); char *key=json_strdup(&doc,json_get(&doc,item,"key")),*text=json_strdup(&doc,json_get(&doc,item,"text"));
        long long cursor=json_ll(&doc,json_get(&doc,item,"cursor"),-1);
        int bad=!key || !text || cursor<0 || cursor>4000 || (key && drafts_get(&fresh,key)) || drafts_set(&fresh,key,text,(int)cursor)<0;
        ar_free(key); ar_free(text); if(bad) goto done;
    }
    drafts_free(d); *d=fresh; memset(&fresh,0,sizeof(fresh)); rc=0;
done: drafts_free(&fresh); json_free(&doc); return rc;
}

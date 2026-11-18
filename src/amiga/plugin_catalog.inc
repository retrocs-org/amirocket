/* Metadata is read without executing a plugin. Configuration stays in Prefs. */
#define CATALOG_LIMIT 32
typedef struct { char id[32],name[64],version[24],author[64],about[192],path[256]; } PluginEntry;
typedef struct { PluginEntry entry[CATALOG_LIMIT]; int count; struct List nodes; } PluginCatalog;
static void catalog_add(PluginCatalog *c,const char *id,const char *path)
{
    PluginEntry *e; BPTR f; char meta[280],data[1025],*line; LONG n; int i;
    if(!id[0] || strlen(id)>31 || c->count==CATALOG_LIMIT) return;
    for(i=0;id[i];i++) if(!((id[i]>='a'&&id[i]<='z')||(id[i]>='0'&&id[i]<='9')||id[i]=='_')) return;
    for(i=0;i<c->count;i++) if(!strcmp(c->entry[i].id,id)) return;
    e=&c->entry[c->count++]; snprintf(e->id,sizeof(e->id),"%s",id); snprintf(e->name,sizeof(e->name),"%s",id);
    snprintf(e->path,sizeof(e->path),"%s",path); strcpy(e->version,"unknown"); strcpy(e->author,"Not specified");
    snprintf(meta,sizeof(meta),"%s.meta",path); f=Open(meta,MODE_OLDFILE); if(!f) return;
    n=Read(f,data,1024); Close(f); if(n<0) return; data[n]=0;
    line=data;
    while(line && *line) {
        char *next=strchr(line,'\n'),*value=strchr(line,'='); if(next) *next++=0;
        if(value) { *value++=0; value[strcspn(value,"\r")]=0;
            if(!strcmp(line,"name")) ar_strlcpy(e->name,value,sizeof(e->name));
            else if(!strcmp(line,"version")) ar_strlcpy(e->version,value,sizeof(e->version));
            else if(!strcmp(line,"author")) ar_strlcpy(e->author,value,sizeof(e->author));
            else if(!strcmp(line,"description")) ar_strlcpy(e->about,value,sizeof(e->about));
        } line=next;
    }
}
static void catalog_scan(PluginCatalog *c,const Prefs *prefs)
{
    int i; BPTR lock; struct FileInfoBlock *fib; char id[32],path[256];
    memset(c,0,sizeof(*c)); NewList(&c->nodes);
    /* Configured paths take precedence over files discovered in PROGDIR. */
    for(i=0;i<prefs->extra_count;i++) {
        const char *key=prefs->extra[i].key; size_t n=strlen(key);
        if(n>12 && n<44 && !strncmp(key,"plugin.",7) && !strcmp(key+n-5,".path")) {
            memcpy(id,key+7,n-12); id[n-12]=0; catalog_add(c,id,prefs->extra[i].value);
        }
    }
    lock=Lock("PROGDIR:Plugins",ACCESS_READ); fib=AllocDosObject(DOS_FIB,NULL);
    if(lock && fib && Examine(lock,fib)) while(ExNext(lock,fib)) {
        size_t n=strlen(fib->fib_FileName);
        if(fib->fib_DirEntryType<0 && n>4 && n<36 && !ar_strcasecmp(fib->fib_FileName+n-4,".arp")) {
            memcpy(id,fib->fib_FileName,n-4); id[n-4]=0;
            snprintf(path,sizeof(path),"PROGDIR:Plugins/%s",fib->fib_FileName); catalog_add(c,id,path);
        }
    }
    if(fib) FreeDosObject(DOS_FIB,fib);
    if(lock) UnLock(lock);
}
static void catalog_rows(PluginCatalog *c,const Prefs *prefs)
{
    int i,running; char active[272];
    running=prefs_runtime_plugins(active,sizeof(active)); free_list(&c->nodes);
    for(i=0;i<c->count;i++) {
        PluginEntry *e=&c->entry[i]; char key[80],match[36]; const char *state; struct Node *node;
        snprintf(key,sizeof(key),"plugin.%s.enabled",e->id); snprintf(match,sizeof(match),"|%s|",e->id);
        state=!strcmp(prefs_extra(prefs,key,"0"),"1") ? (running ? (strstr(active,match)?"Active":"Enabled / pending") : "Enabled") : "Disabled";
        node=AllocListBrowserNode(3,LBNA_Column,0,LBNCA_CopyText,TRUE,LBNCA_Text,(ULONG)e->name,
            LBNA_Column,1,LBNCA_CopyText,TRUE,LBNCA_Text,(ULONG)e->version,
            LBNA_Column,2,LBNCA_CopyText,TRUE,LBNCA_Text,(ULONG)state,TAG_END);
        if(node) AddTail(&c->nodes,node);
    }
}

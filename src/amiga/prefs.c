#include "prefs.h"
#include "../core/mem.h"
#include "../core/buf.h"
#include "../core/mem.h"
#include "../core/util.h"
#include "../core/url.h"

#include <dos/dos.h>
#include <dos/var.h>
#include <exec/semaphores.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREFS_NAME "AmiRocket.prefs"

static char config_override[512];
int prefs_config_override(const char *path)
{
    if(!path || !strchr(path,':') || strlen(path)>=sizeof(config_override) || strpbrk(path,"\r\n\"*")) return -1;
    ar_strlcpy(config_override,path,sizeof(config_override)); return 0;
}
int prefs_config_file(char *out,int size)
{
    char path[512]={0}; LONG n; BPTR f;
    if(config_override[0]) { ar_strlcpy(out,config_override,size); return 0; }
    n=GetVar("AmiRocket-ConfigFile",path,sizeof(path),GVF_GLOBAL_ONLY);
    if(n<0) {
        f=Open("ENVARC:AmiRocket-ConfigFile",MODE_OLDFILE);
        if(f) { n=Read(f,path,sizeof(path)-1); Close(f); if(n>=0)path[n]=0; }
    }
    if(n>0 && n<(LONG)sizeof(path)-1 && strchr(path,':') && !strpbrk(path,"\r\n\"*")) {
        ar_strlcpy(out,path,size); return 0;
    }
    if(n>=0) return -1; /* Invalid location must never overwrite the default file. */
    ar_strlcpy(out,"ENVARC:" PREFS_NAME,size); return 0;
}

/* One OS-session semaphore serializes all clients and the separate editor.
 * Its storage/name must outlive any one executable. Nested locks are allowed. */
static struct SignalSemaphore *config_mutex;
int prefs_lock(void)
{
    if(!config_mutex) {
        struct SharedLock { struct SignalSemaphore sem; char name[32]; } *candidate;
        candidate=AllocVec(sizeof(*candidate),MEMF_PUBLIC|MEMF_CLEAR);
        if(!candidate) return -1;
        strcpy(candidate->name,"AmiRocket.Config.File.v1"); InitSemaphore(&candidate->sem);
        candidate->sem.ss_Link.ln_Name=candidate->name;
        Forbid(); config_mutex=FindSemaphore(candidate->name);
        if(!config_mutex) { AddSemaphore(&candidate->sem); config_mutex=&candidate->sem; candidate=NULL; }
        Permit(); if(candidate) FreeVec(candidate);
    }
    ObtainSemaphore(config_mutex); return 0;
}
void prefs_unlock(void) { if(config_mutex) ReleaseSemaphore(config_mutex); }

void prefs_defaults(Prefs *p)
{
    memset(p, 0, sizeof(*p));
    { int i; for(i=0;i<PREFS_SERVERS_MAX;i++) { p->servers[i].verify=1; p->servers[i].remember=1; p->servers[i].sound=1; } }
    p->verify = 1;
    p->remember = 1;
    p->interval = 5; p->realtime=1;
    p->message_limit=1000; p->image_cache_kib=8192;
    p->request_spacing_ms=1000; p->background_interval=30; p->avatar_disk_cache=1;
    ar_strlcpy(p->theme, "workbench", sizeof(p->theme));
    ar_strlcpy(p->chat_font, "helvetica.font", sizeof(p->chat_font));
    p->chat_font_size = 13;
    ar_strlcpy(p->cache_dir,"PROGDIR:Cache",sizeof(p->cache_dir)); ar_strlcpy(p->download_dir,"SYS:Downloads",sizeof(p->download_dir));
    ar_strlcpy(p->notification_sound,"PROGDIR:Sounds/Notification.8svx",sizeof(p->notification_sound));
    p->preview_images = 1; p->preview_width=320; p->external_images=1;
    p->show_emoji = p->show_avatars = p->inline_images = p->open_links = 1;
    ar_strlcpy(p->browser, "C:OpenURL", sizeof(p->browser));
    ar_strlcpy(p->image_viewer, "SYS:Utilities/MultiView", sizeof(p->image_viewer));
    p->win_left = -1;
    p->win_top = -1;
    p->win_width = 640;
    p->win_height = 400;
}

void prefs_migrate_servers(Prefs *p)
{
    int i;
    if(p->servers_defined) return;
    p->servers_defined=1;
    for(i=0;i<PREFS_SERVERS_MAX;i++) if(p->servers[i].url[0]) return;
    if(p->server[0]) {
        ServerPrefs *s=&p->servers[0];
        ar_strlcpy(s->url,p->server,sizeof(s->url)); ar_strlcpy(s->user,p->user,sizeof(s->user));
        ar_strlcpy(s->user_id,p->user_id,sizeof(s->user_id)); ar_strlcpy(s->token,p->token,sizeof(s->token));
        s->verify=p->verify; s->remember=p->remember; s->autoconnect=1;
    }
}
int prefs_server_validate(const ServerPrefs *s,char *error,int size)
{
    ServerUrl url;
    if(strpbrk(s->name,"\r\n") || strpbrk(s->user,"\r\n") || strpbrk(s->url,"\r\n \t")) {
        snprintf(error,size,"Server fields cannot contain line breaks or URL spaces."); return -1;
    }
    return url_parse_server(s->url,&url,error,size);
}
int prefs_server_equal(const ServerPrefs *a,const ServerPrefs *b)
{
    return !strcmp(a->url,b->url) && !strcmp(a->name,b->name) && !strcmp(a->user,b->user) &&
        a->verify==b->verify && a->remember==b->remember && a->autoconnect==b->autoconnect && a->notify_mentions==b->notify_mentions && a->quiet==b->quiet && a->sound==b->sound;
}

const char *prefs_extra(const Prefs *p, const char *key, const char *fallback)
{
    int i; for (i = 0; i < p->extra_count; i++) if (!strcmp(p->extra[i].key, key)) return p->extra[i].value;
    return fallback;
}
static int extra_set(Prefs *p, const char *key, const char *value)
{
    int i;
    if (!key || !*key || strlen(key) >= sizeof(p->extra[0].key) || strlen(value) >= sizeof(p->extra[0].value) ||
        strpbrk(key, "= \t\r\n") || strpbrk(value, "\r\n")) return -1;
    for (i = 0; i < p->extra_count && strcmp(p->extra[i].key, key); i++);
    if (i == PREFS_EXTRA_MAX) return -1;
    if (i == p->extra_count) p->extra_count++;
    ar_strlcpy(p->extra[i].key, key, sizeof(p->extra[i].key));
    ar_strlcpy(p->extra[i].value, value, sizeof(p->extra[i].value)); return 0;
}
static const char *editable[] = {"notification_sound", "cache_dir", "download_dir", "auto_thumbnails", "external_images", "preview_width", "realtime", "low_memory", "message_limit", "image_cache_kib", "request_spacing_ms", "background_interval", "avatar_disk_cache", "interval", "theme", "ownscreen", "chat_font", "chat_font_size", "preview_images", "show_emoji", "show_avatars", "inline_images", "open_links", "browser", "image_viewer", NULL};
int prefs_get(const Prefs *p, const char *key, char *out, int size)
{
    int i; if (!out || size < 1) return -1;
    for (i = 0; editable[i] && strcmp(editable[i], key); i++);
    if (!editable[i]) {
        const char *v = !strncmp(key, "plugin.", 7) ? prefs_extra(p, key, NULL) : NULL;
        if (!v) return -1;
        ar_strlcpy(out, v, size); return 0;
    }
    if (!strcmp(key,"notification_sound")) ar_strlcpy(out,p->notification_sound,size);
    else if (!strcmp(key,"cache_dir")) ar_strlcpy(out,p->cache_dir,size);
    else if (!strcmp(key,"download_dir")) ar_strlcpy(out,p->download_dir,size);
    else if (!strcmp(key,"theme")) ar_strlcpy(out,p->theme,size);
    else if (!strcmp(key,"chat_font")) ar_strlcpy(out,p->chat_font,size);
    else if (!strcmp(key,"browser")) ar_strlcpy(out,p->browser,size);
    else if (!strcmp(key,"image_viewer")) ar_strlcpy(out,p->image_viewer,size);
    else {
        int v = !strcmp(key,"auto_thumbnails")?p->auto_thumbnails : !strcmp(key,"external_images")?p->external_images : !strcmp(key,"preview_width")?p->preview_width : !strcmp(key,"realtime")?p->realtime : !strcmp(key,"low_memory")?p->low_memory : !strcmp(key,"message_limit")?p->message_limit : !strcmp(key,"image_cache_kib")?p->image_cache_kib : !strcmp(key,"request_spacing_ms")?p->request_spacing_ms : !strcmp(key,"background_interval")?p->background_interval : !strcmp(key,"avatar_disk_cache")?p->avatar_disk_cache : !strcmp(key,"interval") ? p->interval : !strcmp(key,"chat_font_size") ? p->chat_font_size :
            !strcmp(key,"ownscreen") ? p->ownscreen : !strcmp(key,"preview_images") ? p->preview_images :
            !strcmp(key,"show_emoji") ? p->show_emoji : !strcmp(key,"show_avatars") ? p->show_avatars :
            !strcmp(key,"inline_images") ? p->inline_images : p->open_links;
        snprintf(out,size,"%d",v);
    }
    return 0;
}

static int set_kv(Prefs *p, const char *k, const char *v)
{
    int slot, used=0;
    if(!strcmp(k,"servers_defined")) { p->servers_defined=atoi(v)!=0; return 0; }
    if(sscanf(k,"server.%d.%n",&slot,&used)==1 && used>0 && slot>=0 && slot<PREFS_SERVERS_MAX) {
        ServerPrefs *s=&p->servers[slot]; const char *field=k+used;
        if(!strcmp(field,"url")) ar_strlcpy(s->url,v,sizeof(s->url));
        else if(!strcmp(field,"name")) ar_strlcpy(s->name,v,sizeof(s->name));
        else if(!strcmp(field,"user")) ar_strlcpy(s->user,v,sizeof(s->user));
        else if(!strcmp(field,"user_id")) ar_strlcpy(s->user_id,v,sizeof(s->user_id));
        else if(!strcmp(field,"token")) ar_strlcpy(s->token,v,sizeof(s->token));
        else if(!strcmp(field,"verify")) s->verify=atoi(v)!=0;
        else if(!strcmp(field,"remember")) s->remember=atoi(v)!=0;
        else if(!strcmp(field,"quiet")) s->quiet=atoi(v)!=0;
        else if(!strcmp(field,"sound")) s->sound=atoi(v)!=0;
        else if(!strcmp(field,"notify_mentions")) s->notify_mentions=atoi(v)!=0;
        else if(!strcmp(field,"autoconnect")) s->autoconnect=atoi(v)!=0;
        else return extra_set(p,k,v);
        return 0;
    }
    if (!strcmp(k, "server")) ar_strlcpy(p->server, v, sizeof(p->server));
    else if (!strcmp(k, "user")) ar_strlcpy(p->user, v, sizeof(p->user));
    else if (!strcmp(k, "user_id")) ar_strlcpy(p->user_id, v, sizeof(p->user_id));
    else if (!strcmp(k, "token")) ar_strlcpy(p->token, v, sizeof(p->token));
    else if (!strcmp(k, "verify")) p->verify = atoi(v) != 0;
    else if (!strcmp(k, "remember")) p->remember = atoi(v) != 0;
    else if (!strcmp(k,"realtime")) p->realtime=atoi(v)!=0;
    else if (!strcmp(k,"low_memory")) p->low_memory=atoi(v)!=0;
    else if (!strcmp(k,"message_limit")) p->message_limit=atoi(v);
    else if (!strcmp(k,"image_cache_kib")) p->image_cache_kib=atoi(v);
    else if (!strcmp(k,"request_spacing_ms")) p->request_spacing_ms=atoi(v);
    else if (!strcmp(k,"background_interval")) p->background_interval=atoi(v);
    else if (!strcmp(k,"avatar_disk_cache")) p->avatar_disk_cache=atoi(v)!=0;
    else if (!strcmp(k, "interval")) p->interval = atoi(v);
    else if (!strcmp(k, "theme")) ar_strlcpy(p->theme, v, sizeof(p->theme));
    else if (!strcmp(k, "ownscreen")) p->ownscreen = atoi(v) != 0;
    else if (!strcmp(k, "chat_font")) ar_strlcpy(p->chat_font, v, sizeof(p->chat_font));
    else if (!strcmp(k, "chat_font_size")) p->chat_font_size = atoi(v);
    else if (!strcmp(k,"notification_sound")) ar_strlcpy(p->notification_sound,v,sizeof(p->notification_sound));
    else if (!strcmp(k,"cache_dir")) ar_strlcpy(p->cache_dir,v,sizeof(p->cache_dir));
    else if (!strcmp(k,"download_dir")) ar_strlcpy(p->download_dir,v,sizeof(p->download_dir));
    else if (!strcmp(k,"auto_thumbnails")) p->auto_thumbnails=atoi(v)!=0;
    else if (!strcmp(k,"external_images")) p->external_images=atoi(v)!=0;
    else if (!strcmp(k,"preview_width")) p->preview_width=atoi(v);
    else if (!strcmp(k, "preview_images")) p->preview_images = atoi(v) != 0;
    else if (!strcmp(k, "show_emoji")) p->show_emoji = atoi(v) != 0;
    else if (!strcmp(k, "show_avatars")) p->show_avatars = atoi(v) != 0;
    else if (!strcmp(k, "inline_images")) p->inline_images = atoi(v) != 0;
    else if (!strcmp(k, "open_links")) p->open_links = atoi(v) != 0;
    else if (!strcmp(k, "browser")) ar_strlcpy(p->browser, v, sizeof(p->browser));
    else if (!strcmp(k, "image_viewer")) ar_strlcpy(p->image_viewer, v, sizeof(p->image_viewer));
    else if (!strcmp(k, "window")) {
        sscanf(v, "%d,%d,%d,%d", &p->win_left, &p->win_top, &p->win_width, &p->win_height);
    } else return extra_set(p, k, v);
    return 0;
}

int prefs_set(Prefs *p, const char *key, const char *value)
{
    int i; char *end; long n;
    if (!key || !value || strpbrk(value,"\r\n")) return -1;
    if (!strncmp(key,"plugin.",7) || !strncmp(key,"workspace.",10)) return extra_set(p,key,value);
    for (i = 0; editable[i] && strcmp(editable[i],key); i++);
    if (!editable[i]) return -1;
    if (!strcmp(key,"notification_sound")) {
        if(strlen(value)>=256 || (*value && !strchr(value,':')) || strpbrk(value,"\"*\r\n"))return -1;
    } else if (!strcmp(key,"cache_dir") || !strcmp(key,"download_dir")) {
        if(!*value || !strchr(value,':') || strlen(value)>=256 || strpbrk(value,"\"*\r\n"))return -1;
    } else if (!strcmp(key,"theme") || !strcmp(key,"chat_font") || !strcmp(key,"browser") || !strcmp(key,"image_viewer")) {
        size_t max = !strcmp(key,"theme") ? sizeof(p->theme) : sizeof(p->chat_font);
        if (strlen(value) >= max) return -1;
    } else {
        n = strtol(value,&end,10); if (!*value || *end) return -1;
        if (!strcmp(key,"preview_width")) { if(n!=160 && n!=320 && n!=640) return -1; }
        else if (!strcmp(key,"interval")) { if (n < 2 || n > 600) return -1; }
        else if (!strcmp(key,"message_limit")) { if(n<50 || n>1000) return -1; }
        else if (!strcmp(key,"image_cache_kib")) { if(n<256 || n>8192) return -1; }
        else if (!strcmp(key,"request_spacing_ms")) { if(n<100 || n>5000) return -1; }
        else if (!strcmp(key,"background_interval")) { if(n<15 || n>600) return -1; }
        else if (!strcmp(key,"chat_font_size")) { if (n < 8 || n > 48) return -1; }
        else if (n != 0 && n != 1) return -1;
    }
    return set_kv(p,key,value);
}

static int prefs_load_unlocked(Prefs *p)
{
    char selected[512]; const char *paths[3];
    if(prefs_config_file(selected,sizeof(selected))<0) { prefs_defaults(p); return -2; }
    paths[0]=selected; paths[1]=!strcmp(selected,"ENVARC:" PREFS_NAME)?"ENV:" PREFS_NAME:NULL; paths[2]=NULL;
    char data[32768];
    BPTR fh = 0;
    LONG n;
    int i;
    char *line, *next;

    prefs_defaults(p);
    for (i = 0; paths[i] && !fh; i++)
        fh = Open((CONST_STRPTR)paths[i], MODE_OLDFILE);
    if (!fh)
        return -1;
    n = Read(fh, data, sizeof(data) - 1);
    if (n == sizeof(data)-1) { char more; if (Read(fh,&more,1)>0) { Close(fh); return -2; } }
    Close(fh);
    if (n <= 0)
        return -1;
    data[n] = 0;
    for (line = data; line && *line; line = next) {
        char *eq;
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        ar_trim(line);
        if (!*line || *line == '#' || !(eq = strchr(line, '=')))
            continue;
        *eq = 0;
        if(set_kv(p, ar_trim(line), ar_trim(eq + 1))<0) return -2;
    }
    prefs_migrate_servers(p);
    if (p->interval < 2 || p->interval > 600) p->interval=5;
    if(p->message_limit<50 || p->message_limit>1000) p->message_limit=1000;
    if(p->preview_width!=160 && p->preview_width!=320 && p->preview_width!=640) p->preview_width=320;
    if(p->image_cache_kib<256 || p->image_cache_kib>8192) p->image_cache_kib=8192;
    if(p->request_spacing_ms<100 || p->request_spacing_ms>5000) p->request_spacing_ms=1000;
    if(p->background_interval<15 || p->background_interval>600) p->background_interval=30;
    if (p->chat_font_size < 8 || p->chat_font_size > 48) p->chat_font_size = 13;
    return 0;
}

static int write_file(const char *path, const Buf *b)
{
    char temp[560], backup[560]; BPTR fh, lock; LONG w; int had_old = 0;
    if (b->oom || strlen(path)>sizeof(temp)-5) return -1;
    snprintf(temp,sizeof(temp),"%s.new",path); snprintf(backup,sizeof(backup),"%s.bak",path);
    fh = Open(temp,MODE_NEWFILE); if (!fh) return -1;
    w = Write(fh,(APTR)buf_str(b),(LONG)b->len);
    if (!Close(fh) || w != (LONG)b->len) { DeleteFile(temp); return -1; }
    lock = Lock(path,ACCESS_READ);
    if (lock) { UnLock(lock); had_old = 1; if (!Rename(path,backup)) { DeleteFile(temp); return -1; } }
    if (!Rename(temp,path)) { if (had_old) Rename(backup,path); DeleteFile(temp); return -1; }
    if (had_old) DeleteFile(backup);
    return 0;
}

static int prefs_save_unlocked(const Prefs *p)
{
    Buf b;
    int rc;

    buf_init(&b);
    buf_puts(&b, "# AmiRocket settings\n");
    buf_printf(&b,"notification_sound=%s\n",p->notification_sound);
    buf_printf(&b,"cache_dir=%s\ndownload_dir=%s\n",p->cache_dir,p->download_dir);
    buf_printf(&b,"auto_thumbnails=%d\n",p->auto_thumbnails);
    buf_printf(&b,"realtime=%d\npreview_width=%d\nexternal_images=%d\n",p->realtime,p->preview_width,p->external_images);
    buf_printf(&b,"low_memory=%d\nmessage_limit=%d\nimage_cache_kib=%d\n",p->low_memory,p->message_limit,p->image_cache_kib);
    buf_printf(&b,"request_spacing_ms=%d\nbackground_interval=%d\navatar_disk_cache=%d\n",p->request_spacing_ms,p->background_interval,p->avatar_disk_cache);
    buf_printf(&b, "server=%s\nuser=%s\nverify=%d\nremember=%d\ninterval=%d\n",
               p->server, p->user, p->verify, p->remember, p->interval);
    buf_printf(&b, "window=%d,%d,%d,%d\n", p->win_left, p->win_top, p->win_width, p->win_height);
    buf_printf(&b, "theme=%s\nownscreen=%d\n", p->theme, p->ownscreen);
    buf_printf(&b, "chat_font=%s\nchat_font_size=%d\npreview_images=%d\n",
               p->chat_font, p->chat_font_size, p->preview_images);
    buf_printf(&b, "show_emoji=%d\nshow_avatars=%d\ninline_images=%d\nopen_links=%d\nbrowser=%s\nimage_viewer=%s\n",
               p->show_emoji, p->show_avatars, p->inline_images, p->open_links, p->browser, p->image_viewer);
    buf_printf(&b,"servers_defined=%d\n",p->servers_defined);
    { int i;
      for(i=0;i<PREFS_SERVERS_MAX;i++) {
        const ServerPrefs *s=&p->servers[i]; if(!s->url[0]) continue;
        buf_printf(&b,"server.%d.url=%s\nserver.%d.name=%s\nserver.%d.user=%s\nserver.%d.verify=%d\nserver.%d.remember=%d\nserver.%d.autoconnect=%d\n",
            i,s->url,i,s->name,i,s->user,i,s->verify,i,s->remember,i,s->autoconnect);
        buf_printf(&b,"server.%d.quiet=%d\nserver.%d.sound=%d\nserver.%d.notify_mentions=%d\n",i,s->quiet,i,s->sound,i,s->notify_mentions);
        if(s->remember && s->token[0]) buf_printf(&b,"server.%d.user_id=%s\nserver.%d.token=%s\n",i,s->user_id,i,s->token);
      }
    }
    if (!p->servers_defined && p->remember && p->token[0])
        buf_printf(&b, "user_id=%s\ntoken=%s\n", p->user_id, p->token);
    { int i;
      for (i = 0; i < p->extra_count; i++) buf_printf(&b,"%s=%s\n",p->extra[i].key,p->extra[i].value);
    }
    { char selected[512]; rc=prefs_config_file(selected,sizeof(selected)); if(!rc) rc=write_file(selected,&b); }
    memset(b.data, 0, b.len);
    buf_free(&b);
    return rc;
}

int prefs_load(Prefs *p)
{
    int rc; if(prefs_lock()<0) { prefs_defaults(p); return -1; }
    rc=prefs_load_unlocked(p); prefs_unlock(); return rc;
}
int prefs_save(const Prefs *p)
{
    int rc; if(prefs_lock()<0) return -1;
    rc=prefs_save_unlocked(p); prefs_unlock(); return rc;
}

/* Direct server editor commits only its entry; other UI/plugin settings stay intact. */
int prefs_save_server(int slot,const ServerPrefs *server)
{
    Prefs *p; ServerPrefs value; int rc; char error[128];
    if(slot<0 || slot>=PREFS_SERVERS_MAX || prefs_server_validate(server,error,sizeof(error))<0) return -1;
    p=ar_malloc(sizeof(*p)); if(!p) return -1;
    if(prefs_lock()<0) { ar_free(p); return -1; }
    rc=prefs_load_unlocked(p);
    if(rc==-2) { prefs_unlock(); ar_free(p); return -1; }
    prefs_migrate_servers(p); value=*server;
    if(value.remember && !strcmp(value.url,p->servers[slot].url) && !strcmp(value.user,p->servers[slot].user)) {
        ar_strlcpy(value.token,p->servers[slot].token,sizeof(value.token)); ar_strlcpy(value.user_id,p->servers[slot].user_id,sizeof(value.user_id));
    }
    if(!value.remember) value.token[0]=value.user_id[0]=0;
    p->servers[slot]=value; p->servers_defined=1; rc=prefs_save_unlocked(p);
    prefs_unlock(); memset(p,0,sizeof(*p)); ar_free(p); return rc;
}

/* Installed resources share one recorded base; unpacked builds retain PROGDIR. */
int prefs_resource_path(const Prefs *p,const char *path,char *out,int size)
{
    const char *base=prefs_extra(p,"workspace.data_dir","");
    if(!path || size<1)return -1;
    if(base[0] && !ar_strncasecmp(path,"PROGDIR:",8)) {
        if(strlen(base)+strlen(path+8)+2>(unsigned)size)return -1;
        ar_strlcpy(out,base,size);return AddPart(out,path+8,size)?0:-1;
    }
    if(strlen(path)>=(unsigned)size)return -1;
    ar_strlcpy(out,path,size);return 0;
}

void prefs_low_resource(Prefs *p)
{
    p->low_memory=1;p->message_limit=100;p->image_cache_kib=256;
    p->show_avatars=0;p->show_emoji=0;p->auto_thumbnails=0;p->preview_width=160;
    p->interval=60;p->background_interval=300;p->request_spacing_ms=1500;
    p->realtime=1;p->avatar_disk_cache=1;
}

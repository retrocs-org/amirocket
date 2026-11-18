#include "rc_api.h"
#include "json.h"
#include "mem.h"
#include "util.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    HttpResponse http;
    JDoc doc;
    int  parsed;
} RcResp;

static void resp_init(RcResp *r)
{
    http_response_init(&r->http);
    memset(&r->doc, 0, sizeof(r->doc));
    r->parsed = 0;
}

static void resp_free(RcResp *r)
{
    json_free(&r->doc);
    http_response_free(&r->http);
    r->parsed = 0;
}

const char *rc_strerror(int code)
{
    switch (code) {
    case RC_OK:               return "OK";
    case RC_ERR_NET:          return "Network error";
    case RC_ERR_HTTP:         return "Unexpected HTTP response";
    case RC_ERR_AUTH:         return "Authentication failed";
    case RC_ERR_TOTP:         return "Two-factor code required";
    case RC_ERR_TOTP_INVALID: return "Two-factor code was not accepted";
    case RC_ERR_PROTO:        return "Unexpected server response";
    case RC_ERR_SERVER:       return "Server reported an error";
    case RC_ERR_NOMEM:        return "Out of memory";
    case RC_ERR_ARGS:         return "Invalid arguments";
    }
    return "Unknown error";
}

const char *rc_error(const RcClient *c)
{
    return c->err[0] ? c->err : "No error";
}

int rc_init(RcClient *c, const NetIface *net, const char *server_url, int verify_tls)
{
    memset(c, 0, sizeof(*c));
    if (url_parse_server(server_url, &c->url, c->err, (int)sizeof(c->err)) < 0)
        return RC_ERR_ARGS;
    http_init(&c->http, net, c->url.host, c->url.port, c->url.tls, verify_tls);
    return RC_OK;
}

void rc_cleanup(RcClient *c)
{
    http_close(&c->http);
    memset(c->token, 0, sizeof(c->token));
}

static int set_err(RcClient *c, int code, const char *fmt, const char *arg);

/* File routes use browser cookies as well as REST headers. Only the initial
 * URL may use workspace credentials; HTTPS storage redirects are credential-free. */
static int download_url(const RcClient *c,const char *url,const ServerUrl *previous,const char *previous_path,ServerUrl *origin,Buf *path)
{
    const char *authority,*end; char base[320],err[80]; size_t n;
    if(!url || !*url || strlen(url)>2047 || strpbrk(url,"\r\n\t ") || strchr(url,'#')) return -1;
    buf_clear(path);
    if(!ar_strncasecmp(url,"http://",7)||!ar_strncasecmp(url,"https://",8)||!strncmp(url,"//",2)) {
        authority=strstr(url,"://"); authority=authority?authority+3:url+2;
        end=authority; while(*end && *end!='/' && *end!='?') end++;
        if(memchr(authority,'@',(size_t)(end-authority))) return -1;
        n=(size_t)(end-url);
        if(!strncmp(url,"//",2)) {
            if(n+7>=sizeof(base)) return -1;
            snprintf(base,sizeof(base),"%s:%.*s",previous&&previous->tls?"https":"http",(int)n,url);
        } else { if(n>=sizeof(base)) return -1; memcpy(base,url,n); base[n]=0; }
        if(url_parse_server(base,origin,err,sizeof(err))<0) return -1;
        if(*end!='/') buf_putc(path,'/');
        buf_puts(path,end);
    } else {
        if(strchr(url,':')) return -1;
        *origin=previous?*previous:c->url;
        if(*url=='/') buf_puts(path,url);
        else if(previous_path) {
            const char *q=strchr(previous_path,'?'),*slash; size_t len=q?(size_t)(q-previous_path):strlen(previous_path);
            if(*url=='?') buf_append(path,previous_path,len);
            else {
                slash=previous_path+len; while(slash>previous_path && slash[-1]!='/') slash--;
                buf_append(path,previous_path,(size_t)(slash-previous_path));
            }
            buf_puts(path,url);
        } else { buf_puts(path,c->url.base); buf_putc(path,'/'); buf_puts(path,url); }
    }
    return path->oom?-1:0;
}
static int same_origin(const ServerUrl *a,const ServerUrl *b)
{
    return !ar_strcasecmp(a->host,b->host) && a->port==b->port && a->tls==b->tls;
}
int rc_download(RcClient *c,const char *url,Buf *data)
{
    Buf path,next_path,hdr; HttpResponse response; HttpClient external;
    ServerUrl origin,next_origin; size_t saved_max; int rc=RC_ERR_HTTP,hops,auth=1,external_ready=0;
    c->err[0]=0; buf_init(&path); buf_init(&next_path); buf_init(&hdr); http_response_init(&response);
    if(download_url(c,url,NULL,NULL,&origin,&path)<0) { rc=set_err(c,RC_ERR_ARGS,"Invalid attachment URL",NULL); goto done; }
    if(!same_origin(&origin,&c->url)) { rc=set_err(c,RC_ERR_ARGS,"External attachment links are not downloaded directly",NULL); goto done; }
    buf_printf(&hdr,"X-Auth-Token: %s\r\nX-User-Id: %s\r\nCookie: rc_uid=%s; rc_token=%s\r\n",c->token,c->user_id,c->user_id,c->token);
    if(hdr.oom) { rc=RC_ERR_NOMEM; goto done; }
    for(hops=0;hops<4;hops++) {
        HttpClient *http=&c->http;
        if(!same_origin(&origin,&c->url)) {
            auth=0;
            if(!origin.tls) { rc=set_err(c,RC_ERR_HTTP,"Refusing an insecure file redirect",NULL); break; }
            if(external_ready) http_close(&external);
            http_init(&external,c->http.net,origin.host,origin.port,origin.tls,c->http.opt.verify);
            external.progress=c->http.progress; external.progress_ctx=c->http.progress_ctx;
            external_ready=1; http=&external;
        }
        saved_max=http->max_body; http->max_body=c->http.transfer_limit?c->http.transfer_limit:8UL*1024UL*1024UL;
        rc=http_request(http,"GET",buf_str(&path),auth?buf_str(&hdr):NULL,NULL,0,&response);
        http->max_body=saved_max;
        if(rc<0) { rc=set_err(c,RC_ERR_NET,"%s",http->err); break; }
        if(response.status==301||response.status==302||response.status==303||response.status==307||response.status==308) {
            if(hops==3) { rc=set_err(c,RC_ERR_HTTP,"Too many file redirects",NULL); break; }
            if(!response.location[0] || download_url(c,response.location,&origin,buf_str(&path),&next_origin,&next_path)<0) {
                rc=set_err(c,RC_ERR_HTTP,"Invalid or missing file redirect",NULL); break;
            }
            if(origin.tls && !next_origin.tls) { rc=set_err(c,RC_ERR_HTTP,"Refusing an insecure file redirect",NULL); break; }
            origin=next_origin; buf_clear(&path); buf_puts(&path,buf_str(&next_path));
            if(path.oom) { rc=RC_ERR_NOMEM; break; }
            http_response_free(&response); http_response_init(&response); continue;
        }
        if(response.status!=200) {
            snprintf(c->err,sizeof(c->err),"File download failed (HTTP %d)%s",response.status,response.status==401||response.status==403?" - sign in again or check file access":"");
            rc=RC_ERR_HTTP; break;
        }
        if(strstr(response.content_type,"text/html")) { rc=set_err(c,RC_ERR_HTTP,"Server returned a login/error page instead of a file",NULL); break; }
        buf_free(data); *data=response.body; buf_init(&response.body); rc=RC_OK; break;
    }
done:
    if(external_ready) http_close(&external);
    buf_free(&path); buf_free(&next_path); buf_free(&hdr); http_response_free(&response); return rc;
}

/* ---- dates --------------------------------------------------------- */

static ar_ms json_date(const JDoc *d, int i)
{
    char tmp[40];
    int t = json_type(d, i);

    if (t == JT_STRING) {
        if (json_str(d, i, tmp, sizeof(tmp)) < 0)
            return 0;
        return iso8601_to_ms(tmp);
    }
    if (t == JT_OBJECT)
        return json_ll(d, json_get(d, i, "$date"), 0);
    if (t == JT_NUMBER)
        return json_ll(d, i, 0);
    return 0;
}

/* ---- request plumbing ---------------------------------------------- */

static int set_err(RcClient *c, int code, const char *fmt, const char *arg)
{
    snprintf(c->err, sizeof(c->err), fmt, arg ? arg : "");
    return code;
}

static int rc_call(RcClient *c, const char *method, const char *path, const char *body,
                   int auth, RcResp *r)
{
    Buf full, hdr;
    int rc, root;
    const char *ct;

    c->err[0] = 0;
    c->totp_method[0] = 0;
    buf_init(&full);
    buf_init(&hdr);
    buf_puts(&full, c->url.base);
    buf_puts(&full, path);
    if (auth && c->token[0])
        buf_printf(&hdr, "X-Auth-Token: %s\r\nX-User-Id: %s\r\n", c->token, c->user_id);
    if (full.oom || hdr.oom) {
        buf_free(&full);
        buf_free(&hdr);
        return set_err(c, RC_ERR_NOMEM, "Out of memory", NULL);
    }
    rc = http_request(&c->http, method, buf_str(&full), hdr.len ? buf_str(&hdr) : NULL,
                      body, body ? strlen(body) : 0, &r->http);
    buf_free(&full);
    buf_free(&hdr);
    if (rc < 0)
        return set_err(c, RC_ERR_NET, "%s", c->http.err);

    c->last_status = r->http.status;
    if (r->http.status >= 300 && r->http.status < 400) {
        snprintf(c->err, sizeof(c->err), "Server redirected (HTTP %d) to %s - check the server address",
                 r->http.status, r->http.location[0] ? r->http.location : "another location");
        return RC_ERR_HTTP;
    }

    ct = r->http.content_type;
    if (r->http.body.len == 0 || json_parse(&r->doc, r->http.body.data, r->http.body.len) < 0) {
        if (r->http.status == 401)
            return set_err(c, RC_ERR_AUTH, "Not authorised (HTTP 401)", NULL);
        if (*ct && !strstr(ct, "json")) {
            snprintf(c->err, sizeof(c->err), "HTTP %d, not JSON (%s) - is this a Rocket.Chat server?",
                     r->http.status, ct);
            return RC_ERR_HTTP;
        }
        if (r->http.status >= 400) {
            snprintf(c->err, sizeof(c->err), "HTTP %d from server", r->http.status);
            return RC_ERR_HTTP;
        }
        return set_err(c, RC_ERR_PROTO, "Invalid JSON from server", NULL);
    }
    r->parsed = 1;
    root = 0;

    /* Rocket.Chat signals failure with success:false (most endpoints) or
     * status:"error" (login), usually but not always with an HTTP 4xx. */
    if (json_type(&r->doc, root) == JT_OBJECT &&
        (json_bool(&r->doc, json_get(&r->doc, root, "success"), 1) == 0 ||
         json_eq(&r->doc, json_get(&r->doc, root, "status"), "error") ||
         r->http.status >= 400)) {
        char e[96], m[160];
        int ie = json_get(&r->doc, root, "error");
        int im = json_get(&r->doc, root, "message");

        e[0] = m[0] = 0;
        if (json_type(&r->doc, ie) == JT_STRING)
            json_str(&r->doc, ie, e, sizeof(e));
        if (json_type(&r->doc, im) == JT_STRING)
            json_str(&r->doc, im, m, sizeof(m));

        if (!strcmp(e, "totp-required")) {
            json_str(&r->doc, json_path(&r->doc, root, "details.method"), c->totp_method, sizeof(c->totp_method));
            if (!c->totp_method[0])
                ar_strlcpy(c->totp_method, "totp", sizeof(c->totp_method));
            return set_err(c, RC_ERR_TOTP, "Two-factor code required (%s)", c->totp_method);
        }
        if (!strcmp(e, "totp-invalid"))
            return set_err(c, RC_ERR_TOTP_INVALID, "Two-factor code was not accepted", NULL);

        snprintf(c->err, sizeof(c->err), "%s", m[0] ? m : (e[0] ? e : "Request failed"));
        if (r->http.status >= 400 && !m[0] && !e[0])
            snprintf(c->err, sizeof(c->err), "HTTP %d from server", r->http.status);
        if (r->http.status == 401 || !strcmp(e, "Unauthorized") || !strcmp(e, "error-unauthorized"))
            return RC_ERR_AUTH;
        return RC_ERR_SERVER;
    }
    return RC_OK;
}

/* ---- parsing ------------------------------------------------------- */

static char *dup_field(const JDoc *d, int obj, const char *key)
{
    int i = json_get(d, obj, key);
    return json_type(d, i) == JT_STRING ? json_strdup(d, i) : NULL;
}

static int parse_room(const JDoc *d, int o, RcRoom *r)
{
    char t[4];

    memset(r, 0, sizeof(*r));
    r->rid = dup_field(d, o, "rid");
    if (!r->rid)
        return -1;
    r->name = dup_field(d, o, "name");
    r->fname = dup_field(d, o, "fname");
    if (!r->name)
        r->name = ar_strdup(r->fname ? r->fname : r->rid);
    t[0] = 0;
    json_str(d, json_get(d, o, "t"), t, sizeof(t));
    r->type = t[0] ? t[0] : 'c';
    r->unread = (int)json_ll(d, json_get(d, o, "unread"), 0);
    r->alert = json_bool(d, json_get(d, o, "alert"), 0);
    { long long user=json_ll(d,json_get(d,o,"userMentions"),0),group=json_ll(d,json_get(d,o,"groupMentions"),0);
      if(user<0) user=0;
      if(group<0) group=0;
      if(user>2147483647) user=2147483647;
      if(group>2147483647) group=2147483647;
      r->mentions=user+group>2147483647?2147483647:(int)(user+group);
    }
    r->last_seen_ms=json_date(d,json_get(d,o,"ls"));
    r->open = json_bool(d, json_get(d, o, "open"), 1);
    r->member_count = (int)json_ll(d, json_get(d, o, "usersCount"), 0);
    if (json_type(d, json_get(d, o, "uids")) == JT_ARRAY)
        r->member_count = json_size(d, json_get(d, o, "uids"));
    r->favorite = json_bool(d, json_get(d, o, "f"), 0);
    r->updated_ms = json_date(d, json_get(d, o, "_updatedAt"));
    if (!r->name) {
        rc_room_free(r);
        return -1;
    }
    return 0;
}

static int parse_msg(const JDoc *d, int o, RcMsg *m)
{
    int u, att;

    memset(m, 0, sizeof(*m));
    m->id = dup_field(d, o, "_id");
    if (!m->id)
        return -1;
    m->rid = dup_field(d, o, "rid");
    m->text = dup_field(d, o, "msg");
    if (!m->text)
        m->text = ar_strdup("");
    m->type = dup_field(d, o, "t");
    m->tmid = dup_field(d, o, "tmid");
    m->ts_ms = json_date(d, json_get(d, o, "ts"));
    m->updated_ms = json_date(d, json_get(d, o, "_updatedAt"));
    if (!m->updated_ms)
        m->updated_ms = m->ts_ms;
    m->edited = !json_is_null(d, json_get(d, o, "editedAt"));
    u = json_get(d, o, "u");
    if (u >= 0) {
        m->username = dup_field(d, u, "username");
        m->name = dup_field(d, u, "name");
    }
    if (!m->username)
        m->username = ar_strdup("?");

    att = json_get(d, o, "attachments");
    if (json_type(d, att) == JT_ARRAY && json_size(d, att) > 0) {
        int a0 = json_at(d, att, 0);
        m->n_attachments = json_size(d, att);
        m->attachment = dup_field(d, a0, "title");
        if (!m->attachment)
            m->attachment = dup_field(d, a0, "description");
    }
    if (!m->attachment) {
        int f = json_get(d, o, "file");
        if (f >= 0) {
            m->attachment = dup_field(d, f, "name");
            if (m->attachment && !m->n_attachments)
                m->n_attachments = 1;
        }
    }
    if (json_type(d, att) == JT_ARRAY) {
        int j, n = json_size(d, att);
        if (n > 32) n = 32;
        m->files = (RcAttachment *)ar_calloc((size_t)n, sizeof(RcAttachment));
        if (n && !m->files) { rc_msg_free(m); return -1; }
        for (j = 0; j < n; j++) {
            int a = json_at(d, att, j);
            char *link = dup_field(d, a, "title_link");
            char *image = dup_field(d, a, "image_url");
            RcAttachment *f;
            if (!link || !*link) { ar_free(link); link = image; image = NULL; }
            if (!link) link = dup_field(d, a, "audio_url");
            if (!link) link = dup_field(d, a, "video_url");
            if (!link || !*link) { ar_free(link); ar_free(image); continue; }
            f = &m->files[m->file_count++];
            f->url = link;
            f->size=json_ll(d,json_get(d,a,"size"),0);
            if(f->size<=0) f->size=json_ll(d,json_get(d,a,"image_size"),0);
            if(f->size<0) f->size=0;
            f->title = dup_field(d, a, "title");
            if (!f->title) f->title = ar_strdup(m->attachment ? m->attachment : "attachment");
            f->image = image != NULL || !json_is_null(d, json_get(d, a, "image_url"));
            ar_free(image);
        }
    }
    if(m->file_count==1 && !m->files[0].size) {
        long long size=json_ll(d,json_path(d,o,"file.size"),0); if(size>0) m->files[0].size=size;
    }
    if (!m->file_count) {
        int f = json_get(d, o, "file");
        char *id = dup_field(d, f, "_id");
        char *name = dup_field(d, f, "name");
        char *mime = dup_field(d, f, "type");
        if (id && name) {
            RcAttachment *files = (RcAttachment *)ar_realloc(m->files, sizeof(RcAttachment));
            if (files) {
                Buf link; buf_init(&link); buf_puts(&link, "file-upload/");
                url_encode(&link, id); buf_putc(&link, '/'); url_encode(&link, name);
                m->files = files; memset(files, 0, sizeof(*files));
                files[0].url = buf_detach(&link); files[0].title = ar_strdup(name);
                files[0].image = mime && !strncmp(mime, "image/", 6);
                files[0].size=json_ll(d,json_get(d,f,"size"),0); if(files[0].size<0) files[0].size=0; m->file_count = 1;
                buf_free(&link);
            }
        }
        ar_free(id); ar_free(name); ar_free(mime);
    }
    {   /* "reactions": { ":heart_eyes:": { "usernames": [...] }, ... } */
        int rx = json_get(d, o, "reactions");
        if (json_type(d, rx) == JT_OBJECT && json_size(d, rx) > 0) {
            Buf rb;
            int i = rx + 1, k;
            buf_init(&rb);
            for (k = 0; k < json_size(d, rx); k++) {
                char code[64];
                int val = i + 1, users = json_get(d, val, "usernames");
                if (json_str(d, i, code, sizeof(code)) > 0) {
                    char *c = code;
                    size_t l = strlen(c);
                    if (*c == ':') { c++; l--; }
                    if (l && c[l - 1] == ':') c[--l] = 0;
                    if (rb.len)
                        buf_putc(&rb, '\n');
                    buf_printf(&rb, "%s %d", c, json_type(d, users) == JT_ARRAY ? json_size(d, users) : 1);
                }
                i = d->t[val].next;
            }
            if (rb.len)
                m->reactions = buf_detach(&rb);
            buf_free(&rb);
        }
    }
    if (!m->text || !m->username) {
        rc_msg_free(m);
        return -1;
    }
    return 0;
}

static int list_push_msg(RcMsgList *l, RcMsg *m)
{
    if (l->count == l->cap) {
        int ncap = l->cap ? l->cap * 2 : 32;
        RcMsg *n = (RcMsg *)ar_realloc(l->items, sizeof(RcMsg) * (size_t)ncap);
        if (!n)
            return -1;
        l->items = n;
        l->cap = ncap;
    }
    l->items[l->count++] = *m;
    if (m->updated_ms > l->newest_ms)
        l->newest_ms = m->updated_ms;
    return 0;
}

static int list_push_room(RcRoomList *l, RcRoom *r)
{
    if (l->count == l->cap) {
        int ncap = l->cap ? l->cap * 2 : 32;
        RcRoom *n = (RcRoom *)ar_realloc(l->items, sizeof(RcRoom) * (size_t)ncap);
        if (!n)
            return -1;
        l->items = n;
        l->cap = ncap;
    }
    l->items[l->count++] = *r;
    if (r->updated_ms > l->newest_ms)
        l->newest_ms = r->updated_ms;
    return 0;
}

static int push_str(char ***arr, int *n, char *s)
{
    char **na;

    if (!s)
        return -1;
    na = (char **)ar_realloc(*arr, sizeof(char *) * (size_t)(*n + 1));
    if (!na) {
        ar_free(s);
        return -1;
    }
    na[(*n)++] = s;
    *arr = na;
    return 0;
}

static int parse_msg_array(RcClient *c, const JDoc *d, int arr, RcMsgList *out)
{
    int i, k, n = json_size(d, arr);

    if (json_type(d, arr) != JT_ARRAY)
        return set_err(c, RC_ERR_PROTO, "Expected a message list", NULL);
    for (i = arr + 1, k = 0; k < n; k++, i = d->t[i].next) {
        RcMsg m;
        if (parse_msg(d, i, &m) < 0)
            continue;   /* skip malformed entries */
        if (list_push_msg(out, &m) < 0) {
            rc_msg_free(&m);
            return set_err(c, RC_ERR_NOMEM, "Out of memory", NULL);
        }
    }
    return RC_OK;
}

/* ---- endpoints ----------------------------------------------------- */

int rc_server_info(RcClient *c)
{
    RcResp r;
    int rc;

    resp_init(&r);
    rc = rc_call(c, "GET", "/api/info", NULL, 0, &r);
    if (rc == RC_OK)
        json_str(&r.doc, json_get(&r.doc, 0, "version"), c->server_version, sizeof(c->server_version));
    resp_free(&r);
    return rc;
}

static void read_me(RcClient *c, const JDoc *d, int me)
{
    json_str(d, json_get(d, me, "username"), c->username, sizeof(c->username));
    json_str(d, json_get(d, me, "name"), c->display_name, sizeof(c->display_name));
}

int rc_login_password(RcClient *c, const char *user, const char *password, const char *code)
{
    Buf body;
    RcResp r;
    int rc, data;

    if (!user || !*user || !password)
        return set_err(c, RC_ERR_ARGS, "User name and password are required", NULL);
    c->token[0] = c->user_id[0] = 0;
    buf_init(&body);
    buf_puts(&body, "{\"user\":");
    json_put_string(&body, user);
    buf_puts(&body, ",\"password\":");
    json_put_string(&body, password);
    if (code && *code) {
        buf_puts(&body, ",\"code\":");
        json_put_string(&body, code);
    }
    buf_putc(&body, '}');
    if (body.oom) {
        buf_free(&body);
        return set_err(c, RC_ERR_NOMEM, "Out of memory", NULL);
    }
    resp_init(&r);
    rc = rc_call(c, "POST", "/api/v1/login", buf_str(&body), 0, &r);
    memset(body.data, 0, body.len);   /* don't leave the password lying around */
    buf_free(&body);
    if (rc == RC_OK) {
        data = json_get(&r.doc, 0, "data");
        if (json_str(&r.doc, json_get(&r.doc, data, "authToken"), c->token, sizeof(c->token)) <= 0 ||
            json_str(&r.doc, json_get(&r.doc, data, "userId"), c->user_id, sizeof(c->user_id)) <= 0) {
            rc = set_err(c, RC_ERR_PROTO, "Login response had no token", NULL);
            c->token[0] = c->user_id[0] = 0;
        } else {
            read_me(c, &r.doc, json_get(&r.doc, data, "me"));
            if (!c->username[0])
                ar_strlcpy(c->username, user, sizeof(c->username));
        }
    } else if (rc == RC_ERR_SERVER && c->last_status == 401) {
        rc = RC_ERR_AUTH;
    }
    resp_free(&r);
    return rc;
}

int rc_login_token(RcClient *c, const char *user_id, const char *token)
{
    RcResp r;
    int rc;

    if (!user_id || !*user_id || !token || !*token)
        return set_err(c, RC_ERR_ARGS, "User ID and token are required", NULL);
    ar_strlcpy(c->user_id, user_id, sizeof(c->user_id));
    ar_strlcpy(c->token, token, sizeof(c->token));
    resp_init(&r);
    rc = rc_call(c, "GET", "/api/v1/me", NULL, 1, &r);
    if (rc == RC_OK) {
        read_me(c, &r.doc, 0);
        if (!c->username[0])
            rc = set_err(c, RC_ERR_PROTO, "Server did not return the user profile", NULL);
    }
    if (rc != RC_OK) {
        c->token[0] = c->user_id[0] = 0;
        if (rc == RC_ERR_AUTH)
            set_err(c, RC_ERR_AUTH, "Saved login is no longer valid - please log in again", NULL);
    }
    resp_free(&r);
    return rc;
}

int rc_logout(RcClient *c)
{
    RcResp r;
    int rc;

    if (!c->token[0])
        return RC_OK;
    resp_init(&r);
    rc = rc_call(c, "POST", "/api/v1/logout", NULL, 1, &r);
    resp_free(&r);
    c->token[0] = c->user_id[0] = 0;
    return rc;
}

int rc_presence(RcClient *c, char **out)
{
    RcResp r; Buf text; int rc, users, n, i;
    *out = NULL; resp_init(&r); buf_init(&text);
    rc = rc_call(c, "GET", "/api/v1/users.presence", NULL, 1, &r);
    if (rc != RC_OK) goto done;
    users = json_get(&r.doc, 0, "users");
    if (json_type(&r.doc, users) != JT_ARRAY) { rc = RC_ERR_PROTO; goto done; }
    n = json_size(&r.doc, users); if (n > 1024) n = 1024;
    for (i = 0; i < n; i++) {
        char name[96], status[16]; int u = json_at(&r.doc, users, i);
        if (json_str(&r.doc, json_get(&r.doc, u, "username"), name, sizeof(name)) <= 0 || strpbrk(name, "\t\r\n")) continue;
        if (json_str(&r.doc, json_get(&r.doc, u, "status"), status, sizeof(status)) <= 0) continue;
        if (strcmp(status,"online") && strcmp(status,"away") && strcmp(status,"busy") && strcmp(status,"offline")) continue;
        buf_printf(&text, "%s\t%s\n", name, status);
    }
    if (text.oom) rc = RC_ERR_NOMEM;
    else *out = buf_detach(&text);
done:
    buf_free(&text); resp_free(&r); return rc;
}

int rc_avatar(RcClient *c, const char *username, Buf *data)
{
    Buf path, hdr; HttpResponse r; size_t saved_max; int rc;
    if (!username || !*username || strlen(username) > 95 || strchr(username, ',')) return RC_ERR_ARGS;
    buf_init(&path); buf_init(&hdr); http_response_init(&r);
    buf_puts(&path, c->url.base); buf_puts(&path, "/api/v1/avatar/"); url_encode(&path, username);
    buf_puts(&path, "?format=png&size=32");
    /* This endpoint accepts cookies rather than the usual API auth headers. */
    buf_printf(&hdr, "Cookie: rc_uid=%s; rc_token=%s\r\n", c->user_id, c->token);
    if (path.oom || hdr.oom) { rc = RC_ERR_NOMEM; goto done; }
    saved_max = c->http.max_body; c->http.max_body = 256UL * 1024UL;
    rc = http_request(&c->http, "GET", buf_str(&path), buf_str(&hdr), NULL, 0, &r);
    if (rc == 0 && (r.status == 404 || (r.status == 200 && strstr(r.content_type, "text/html")))) {
        /* Older workspaces expose the same avatar service outside /api/v1. */
        http_response_free(&r); http_response_init(&r); buf_clear(&path);
        buf_puts(&path, c->url.base); buf_puts(&path, "/avatar/"); url_encode(&path, username);
        buf_puts(&path, "?format=png&size=32");
        rc = path.oom ? -1 : http_request(&c->http, "GET", buf_str(&path), buf_str(&hdr), NULL, 0, &r);
    }
    c->http.max_body = saved_max;
    if (rc < 0) { rc = RC_ERR_NET; goto done; }
    if (r.status != 200 || strncmp(r.content_type, "image/", 6) || strstr(r.content_type,"svg")) { rc = RC_ERR_HTTP; goto done; }
    buf_free(data); *data = r.body; buf_init(&r.body); rc = RC_OK;
done:
    buf_free(&path); if (hdr.data) memset(hdr.data, 0, hdr.len);
    buf_free(&hdr); http_response_free(&r); return rc;
}

int rc_dm_participants(const RcRoom *r, const char *me)
{
    const char *p; int n = 0, have_me = 0;
    if (r->type != 'd') return 0;
    if (r->member_count > 2) return r->member_count;
    if (!strchr(r->name, ',')) return 0;
    for (p = r->name; *p;) {
        const char *end = strchr(p, ','), *last;
        if (!end) end = p + strlen(p);
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        last = end; while (last > p && (last[-1] == ' ' || last[-1] == '\t')) last--;
        if (last > p) {
            n++;
            if (me && strlen(me) == (size_t)(last-p) && !ar_strncasecmp(p,me,(size_t)(last-p))) have_me = 1;
        }
        p = *end ? end + 1 : end;
    }
    return n + (have_me ? 0 : 1);
}

int rc_rooms(RcClient *c, ar_ms updated_since, RcRoomList *out)
{
    Buf path;
    RcResp r;
    int rc, arr, i, k, n;

    buf_init(&path);
    buf_puts(&path, "/api/v1/subscriptions.get");
    if (updated_since > 0) {
        char iso[32];
        ms_to_iso8601(updated_since, iso);
        buf_puts(&path, "?updatedSince=");
        url_encode(&path, iso);
    }
    resp_init(&r);
    rc = rc_call(c, "GET", buf_str(&path), NULL, 1, &r);
    buf_free(&path);
    if (rc != RC_OK)
        goto out;

    arr = json_get(&r.doc, 0, "update");
    if (json_type(&r.doc, arr) != JT_ARRAY) {
        rc = set_err(c, RC_ERR_PROTO, "Subscription list missing", NULL);
        goto out;
    }
    n = json_size(&r.doc, arr);
    for (i = arr + 1, k = 0; k < n; k++, i = r.doc.t[i].next) {
        RcRoom room;
        if (parse_room(&r.doc, i, &room) < 0)
            continue;
        if (list_push_room(out, &room) < 0) {
            rc_room_free(&room);
            rc = set_err(c, RC_ERR_NOMEM, "Out of memory", NULL);
            goto out;
        }
    }
    arr = json_get(&r.doc, 0, "remove");
    if (json_type(&r.doc, arr) == JT_ARRAY) {
        n = json_size(&r.doc, arr);
        for (i = arr + 1, k = 0; k < n; k++, i = r.doc.t[i].next) {
            char *rid = dup_field(&r.doc, i, "rid");
            ar_ms upd = json_date(&r.doc, json_get(&r.doc, i, "_deletedAt"));
            if (upd > out->newest_ms)
                out->newest_ms = upd;
            if (rid && push_str(&out->removed, &out->n_removed, rid) < 0) {
                rc = set_err(c, RC_ERR_NOMEM, "Out of memory", NULL);
                goto out;
            }
        }
    }
out:
    resp_free(&r);
    return rc;
}

static const char *history_endpoint(char type)
{
    switch (type) {
    case 'p': return "groups.history";
    case 'd': return "im.history";
    default:  return "channels.history";
    }
}

int rc_history(RcClient *c, const char *rid, char type, int count, ar_ms before, RcMsgList *out)
{
    Buf path;
    RcResp r;
    int rc;

    if (!rid || !*rid)
        return set_err(c, RC_ERR_ARGS, "No room given", NULL);
    buf_init(&path);
    buf_printf(&path, "/api/v1/%s?roomId=", history_endpoint(type));
    url_encode(&path, rid);
    buf_printf(&path, "&count=%d", count > 0 ? count : 50);
    if (before > 0) {
        char iso[32];
        ms_to_iso8601(before, iso);
        buf_puts(&path, "&latest=");
        url_encode(&path, iso);
    }
    if (before > 0) buf_puts(&path, "&inclusive=false");
    resp_init(&r);
    rc = rc_call(c, "GET", buf_str(&path), NULL, 1, &r);
    buf_free(&path);
    if (rc == RC_OK)
        rc = parse_msg_array(c, &r.doc, json_get(&r.doc, 0, "messages"), out);
    resp_free(&r);
    return rc;
}

int rc_sync(RcClient *c, const char *rid, ar_ms last_update, RcMsgList *out)
{
    Buf path;
    RcResp r;
    char iso[32];
    int rc, res, del, i, k, n;

    if (!rid || !*rid || last_update <= 0)
        return set_err(c, RC_ERR_ARGS, "Sync needs a room and a timestamp", NULL);
    ms_to_iso8601(last_update, iso);
    buf_init(&path);
    buf_puts(&path, "/api/v1/chat.syncMessages?roomId=");
    url_encode(&path, rid);
    buf_puts(&path, "&lastUpdate=");
    url_encode(&path, iso);
    resp_init(&r);
    rc = rc_call(c, "GET", buf_str(&path), NULL, 1, &r);
    buf_free(&path);
    if (rc != RC_OK)
        goto out;
    res = json_get(&r.doc, 0, "result");
    if (res < 0) {
        rc = set_err(c, RC_ERR_PROTO, "Sync response missing result", NULL);
        goto out;
    }
    rc = parse_msg_array(c, &r.doc, json_get(&r.doc, res, "updated"), out);
    if (rc != RC_OK)
        goto out;
    del = json_get(&r.doc, res, "deleted");
    if (json_type(&r.doc, del) == JT_ARRAY) {
        n = json_size(&r.doc, del);
        for (i = del + 1, k = 0; k < n; k++, i = r.doc.t[i].next) {
            char *id = dup_field(&r.doc, i, "_id");
            ar_ms when = json_date(&r.doc, json_get(&r.doc, i, "_deletedAt"));
            if (when > out->newest_ms)
                out->newest_ms = when;
            if (id && push_str(&out->deleted, &out->n_deleted, id) < 0) {
                rc = set_err(c, RC_ERR_NOMEM, "Out of memory", NULL);
                goto out;
            }
        }
    }
out:
    resp_free(&r);
    return rc;
}

int rc_send(RcClient *c, const char *rid, const char *text, RcMsgList *echo)
{
    Buf body;
    RcResp r;
    int rc, m;

    if (!rid || !*rid || !text || !*text)
        return set_err(c, RC_ERR_ARGS, "Nothing to send", NULL);
    buf_init(&body);
    buf_puts(&body, "{\"message\":{\"rid\":");
    json_put_string(&body, rid);
    buf_puts(&body, ",\"msg\":");
    json_put_string(&body, text);
    buf_puts(&body, "}}");
    if (body.oom) {
        buf_free(&body);
        return set_err(c, RC_ERR_NOMEM, "Out of memory", NULL);
    }
    resp_init(&r);
    rc = rc_call(c, "POST", "/api/v1/chat.sendMessage", buf_str(&body), 1, &r);
    if(rc==RC_ERR_NET && c->http.sent) {
        char detail[128]; ar_strlcpy(detail,c->err,sizeof(detail));
        snprintf(c->err,sizeof(c->err),"Delivery unconfirmed; check the conversation before resending. %.120s",detail);
    }
    buf_free(&body);
    if (rc == RC_OK && echo) {
        RcMsg msg;
        m = json_get(&r.doc, 0, "message");
        if (m >= 0 && parse_msg(&r.doc, m, &msg) == 0) {
            if (list_push_msg(echo, &msg) < 0)
                rc_msg_free(&msg);
        }
    }
    resp_free(&r);
    return rc;
}

int rc_create_room(RcClient *c, char type, const char *name, const char *members, RcRoomList *out)
{
    Buf body, users; RcResp r; int rc, o, count = 0; const char *p;
    const char *path = type == 'c' ? "/api/v1/channels.create" : type == 'p' ? "/api/v1/groups.create" : "/api/v1/im.create";
    if ((type != 'c' && type != 'p' && type != 'd') || !out)
        return set_err(c, RC_ERR_ARGS, "Invalid conversation type", NULL);
    if (type != 'd' && (!name || !*name || strlen(name) > 128 || strpbrk(name, " \t\r\n")))
        return set_err(c, RC_ERR_ARGS, "Enter a channel name without spaces", NULL);
    if (members && strlen(members) > 3000) return set_err(c, RC_ERR_ARGS, "Too many usernames", NULL);
    buf_init(&body); buf_init(&users);
    buf_puts(&body, type == 'd' ? "{\"usernames\":" : "{\"name\":");
    if (type != 'd') { json_put_string(&body, name); buf_puts(&body, ",\"members\":["); }
    p = members ? members : "";
    while (*p) {
        const char *end = strchr(p, ','); char user[97]; size_t len; const char *a = p;
        if (!end) end = p + strlen(p);
        while (a < end && (*a == ' ' || *a == '\t' || *a == '@')) a++;
        len = end - a; while (len && (a[len-1] == ' ' || a[len-1] == '\t')) len--;
        if (len) {
            size_t j;
            if (len > 96 || count >= 32) goto invalid;
            for (j = 0; j < len; j++) if ((unsigned char)a[j] <= 32 || a[j] == '"' || a[j] == '\\') goto invalid;
            memcpy(user, a, len); user[len] = 0;
            if (count++) { buf_putc(&users, ','); if (type != 'd') buf_putc(&body, ','); }
            buf_puts(&users, user); if (type != 'd') json_put_string(&body, user);
        }
        p = *end ? end + 1 : end;
    }
    if (type == 'd') { if (!count) goto invalid; json_put_string(&body, buf_str(&users)); }
    else buf_putc(&body, ']');
    buf_putc(&body, '}');
    if (body.oom || users.oom) { rc = set_err(c, RC_ERR_NOMEM, "Out of memory", NULL); goto finish; }
    resp_init(&r); rc = rc_call(c, "POST", path, buf_str(&body), 1, &r);
    if (rc == RC_OK) {
        RcRoom room; char rid[64];
        o = json_get(&r.doc, 0, type == 'c' ? "channel" : type == 'p' ? "group" : "room");
        if (json_str(&r.doc, json_get(&r.doc, o, "_id"), rid, sizeof(rid)) <= 0 &&
            json_str(&r.doc, json_get(&r.doc, o, "rid"), rid, sizeof(rid)) <= 0) {
            rc = set_err(c, RC_ERR_PROTO, "Server did not return a room id", NULL);
        } else {
            memset(&room, 0, sizeof(room));
            room.rid = ar_strdup(rid); room.type = type; room.open = 1;
            room.name = ar_strdup(type == 'd' ? buf_str(&users) : name);
            room.member_count = count + 1;
            if (!room.rid || !room.name || list_push_room(out, &room) < 0) {
                rc_room_free(&room); rc = set_err(c, RC_ERR_NOMEM, "Out of memory", NULL);
            }
        }
    }
    resp_free(&r);
finish:
    buf_free(&body); buf_free(&users); return rc;
invalid:
    rc = set_err(c, RC_ERR_ARGS, "Enter up to 32 comma-separated usernames", NULL); goto finish;
}

int rc_mark_read(RcClient *c, const char *rid)
{
    Buf body;
    RcResp r;
    int rc;

    buf_init(&body);
    buf_puts(&body, "{\"rid\":");
    json_put_string(&body, rid);
    buf_putc(&body, '}');
    resp_init(&r);
    rc = rc_call(c, "POST", "/api/v1/subscriptions.read", buf_str(&body), 1, &r);
    buf_free(&body);
    resp_free(&r);
    return rc;
}

int rc_room_topic(RcClient *c, const char *rid, char *out, size_t outsz)
{
    Buf path;
    RcResp r;
    int rc;

    out[0] = 0;
    buf_init(&path);
    buf_puts(&path, "/api/v1/rooms.info?roomId=");
    url_encode(&path, rid);
    resp_init(&r);
    rc = rc_call(c, "GET", buf_str(&path), NULL, 1, &r);
    buf_free(&path);
    if (rc == RC_OK) {
        int t = json_path(&r.doc, 0, "room.topic");
        if (json_type(&r.doc, t) == JT_STRING && json_str(&r.doc, t, out, outsz) < 0)
            out[0] = 0;   /* longer than the buffer: leave it out */
    }
    resp_free(&r);
    return rc;
}

int rc_room_members(RcClient *c, const char *rid, char type, int count, char **out)
{
    Buf path, res;
    RcResp r;
    int rc;

    *out = NULL;
    buf_init(&path);
    buf_printf(&path, "/api/v1/%s?roomId=", type == 'p' ? "groups.members" : type == 'd' ? "im.members" : "channels.members");
    url_encode(&path, rid);
    buf_printf(&path, "&count=%d", count > 0 ? count : 100);
    resp_init(&r);
    rc = rc_call(c, "GET", buf_str(&path), NULL, 1, &r);
    buf_free(&path);
    if (rc == RC_OK) {
        int arr = json_get(&r.doc, 0, "members"), i, k;
        buf_init(&res);
        if (json_type(&r.doc, arr) == JT_ARRAY) {
            for (i = arr + 1, k = 0; k < json_size(&r.doc, arr); k++, i = r.doc.t[i].next) {
                char u[96], n[96], st[16];
                if (json_str(&r.doc, json_get(&r.doc, i, "username"), u, sizeof(u)) <= 0)
                    continue;
                if (json_str(&r.doc, json_get(&r.doc, i, "name"), n, sizeof(n)) < 0)
                    n[0] = 0;
                if (json_str(&r.doc, json_get(&r.doc, i, "status"), st, sizeof(st)) < 0)
                    st[0] = 0;
                buf_printf(&res, "%s\t%s\t%s\n", u, n, st);
            }
        }
        if (res.oom) {
            buf_free(&res);
            rc = set_err(c, RC_ERR_NOMEM, "Out of memory", NULL);
        } else {
            *out = buf_detach(&res);
        }
    }
    resp_free(&r);
    return rc;
}

/* ---- model --------------------------------------------------------- */

void rc_room_free(RcRoom *r)
{
    ar_free(r->rid);
    ar_free(r->name);
    ar_free(r->fname);
    memset(r, 0, sizeof(*r));
}

void rc_roomlist_init(RcRoomList *l)
{
    memset(l, 0, sizeof(*l));
}

static void free_strs(char **a, int n)
{
    int i;
    for (i = 0; i < n; i++)
        ar_free(a[i]);
    ar_free(a);
}

void rc_roomlist_free(RcRoomList *l)
{
    int i;
    for (i = 0; i < l->count; i++)
        rc_room_free(&l->items[i]);
    ar_free(l->items);
    free_strs(l->removed, l->n_removed);
    memset(l, 0, sizeof(*l));
}

const char *rc_room_title(const RcRoom *r)
{
    if (r->fname && *r->fname)
        return r->fname;
    return r->name ? r->name : "?";
}

void rc_msg_free(RcMsg *m)
{
    ar_free(m->id);
    ar_free(m->rid);
    ar_free(m->username);
    ar_free(m->name);
    ar_free(m->text);
    ar_free(m->type);
    ar_free(m->tmid);
    ar_free(m->attachment);
    { int i; for (i = 0; i < m->file_count; i++) {
        ar_free(m->files[i].title); ar_free(m->files[i].url);
    } ar_free(m->files); }
    ar_free(m->reactions);
    memset(m, 0, sizeof(*m));
}

void rc_msglist_init(RcMsgList *l)
{
    memset(l, 0, sizeof(*l));
}

void rc_msglist_free(RcMsgList *l)
{
    int i;
    for (i = 0; i < l->count; i++)
        rc_msg_free(&l->items[i]);
    ar_free(l->items);
    free_strs(l->deleted, l->n_deleted);
    memset(l, 0, sizeof(*l));
}

static int cmp_msg(const void *a, const void *b)
{
    const RcMsg *x = (const RcMsg *)a, *y = (const RcMsg *)b;
    if (x->ts_ms < y->ts_ms) return -1;
    if (x->ts_ms > y->ts_ms) return 1;
    return strcmp(x->id, y->id);
}

int rc_msglist_merge(RcMsgList *dst, RcMsgList *src, int max_keep)
{
    int i, j;

    for (i = 0; i < src->n_deleted; i++) {
        for (j = 0; j < dst->count; j++) {
            if (!strcmp(dst->items[j].id, src->deleted[i])) {
                rc_msg_free(&dst->items[j]);
                memmove(&dst->items[j], &dst->items[j + 1], sizeof(RcMsg) * (size_t)(dst->count - j - 1));
                dst->count--;
                break;
            }
        }
    }
    for (i = 0; i < src->count; i++) {
        RcMsg *m = &src->items[i];
        int found = 0;
        for (j = dst->count - 1; j >= 0; j--) {
            if (!strcmp(dst->items[j].id, m->id)) {
                rc_msg_free(&dst->items[j]);
                dst->items[j] = *m;
                found = 1;
                break;
            }
        }
        if (!found && list_push_msg(dst, m) < 0) {
            /* out of memory: drop the rest */
            for (; i < src->count; i++)
                rc_msg_free(&src->items[i]);
            break;
        }
        memset(m, 0, sizeof(*m));
    }
    if (src->newest_ms > dst->newest_ms)
        dst->newest_ms = src->newest_ms;
    for (i = 0; i < dst->count; i++)
        if (dst->items[i].updated_ms > dst->newest_ms)
            dst->newest_ms = dst->items[i].updated_ms;
    src->count = 0;
    free_strs(src->deleted, src->n_deleted);
    src->deleted = NULL;
    src->n_deleted = 0;

    if (dst->count > 1)
        qsort(dst->items, (size_t)dst->count, sizeof(RcMsg), cmp_msg);
    if (max_keep > 0 && dst->count > max_keep) {
        int drop = dst->count - max_keep;
        for (i = 0; i < drop; i++)
            rc_msg_free(&dst->items[i]);
        memmove(dst->items, dst->items + drop, sizeof(RcMsg) * (size_t)max_keep);
        dst->count = max_keep;
    }
    return 0;
}

int rc_roomlist_merge(RcRoomList *dst, RcRoomList *src)
{
    int i, j;

    for (i = 0; i < src->n_removed; i++) {
        for (j = 0; j < dst->count; j++) {
            if (!strcmp(dst->items[j].rid, src->removed[i])) {
                rc_room_free(&dst->items[j]);
                memmove(&dst->items[j], &dst->items[j + 1], sizeof(RcRoom) * (size_t)(dst->count - j - 1));
                dst->count--;
                break;
            }
        }
    }
    for (i = 0; i < src->count; i++) {
        RcRoom *r = &src->items[i];
        int found = 0;
        for (j = 0; j < dst->count; j++) {
            if (!strcmp(dst->items[j].rid, r->rid)) {
                rc_room_free(&dst->items[j]);
                dst->items[j] = *r;
                found = 1;
                break;
            }
        }
        if (!found && list_push_room(dst, r) < 0) {
            for (; i < src->count; i++)
                rc_room_free(&src->items[i]);
            break;
        }
        memset(r, 0, sizeof(*r));
    }
    if (src->newest_ms > dst->newest_ms)
        dst->newest_ms = src->newest_ms;
    for (i = 0; i < dst->count; i++)
        if (dst->items[i].updated_ms > dst->newest_ms)
            dst->newest_ms = dst->items[i].updated_ms;
    src->count = 0;
    free_strs(src->removed, src->n_removed);
    src->removed = NULL;
    src->n_removed = 0;
    rc_roomlist_sort(dst);
    return 0;
}

int rc_roomlist_copy(RcRoomList *dst, const RcRoomList *src)
{
    int i;

    rc_roomlist_init(dst);
    for (i = 0; i < src->count; i++) {
        RcRoom r = src->items[i];
        r.rid = ar_strdup(r.rid);
        r.name = ar_strdup(r.name);
        r.fname = r.fname ? ar_strdup(r.fname) : NULL;
        if (!r.rid || !r.name || (src->items[i].fname && !r.fname) || list_push_room(dst, &r) < 0) {
            rc_room_free(&r);
            rc_roomlist_free(dst);
            return -1;
        }
    }
    for (i = 0; i < src->n_removed; i++) {
        if (push_str(&dst->removed, &dst->n_removed, ar_strdup(src->removed[i])) < 0) {
            rc_roomlist_free(dst);
            return -1;
        }
    }
    dst->newest_ms = src->newest_ms;
    return 0;
}

static int type_rank(char t)
{
    switch (t) {
    case 'c': return 0;
    case 'p': return 0;
    case 'd': return 1;
    default:  return 2;
    }
}

/* Title for sorting: leading punctuation such as "@" or "#" is ignored, so
 * "@Styrbjorn ..." sorts under S as in the Rocket.Chat client. */
static const char *sort_key(const RcRoom *r)
{
    const char *t = r->type == 'd' ? r->name : rc_room_title(r);
    while (*t && !((*t >= 'a' && *t <= 'z') || (*t >= 'A' && *t <= 'Z') ||
                   (*t >= '0' && *t <= '9') || (unsigned char)*t >= 0x80))
        t++;
    return t;
}

int rc_room_compare(const RcRoom *x, const RcRoom *y)
{
    int d = ar_strcasecmp(sort_key(x), sort_key(y));
    return d ? d : strcmp(x->rid, y->rid);
}

static int cmp_room(const void *a, const void *b)
{
    const RcRoom *x = (const RcRoom *)a, *y = (const RcRoom *)b;
    int d = type_rank(x->type) - type_rank(y->type);
    if (d)
        return d;
    return rc_room_compare(x, y);
}

void rc_roomlist_sort(RcRoomList *l)
{
    if (l->count > 1)
        qsort(l->items, (size_t)l->count, sizeof(RcRoom), cmp_room);
}

const char *rc_system_text(const char *type)
{
    static const struct { const char *t, *s; } tab[] = {
        { "uj", "joined the room" },
        { "ul", "left the room" },
        { "ujt", "joined the team" },
        { "ult", "left the team" },
        { "ut", "joined the conversation" },
        { "au", "added" },
        { "ru", "removed" },
        { "added-user-to-team", "added to the team" },
        { "removed-user-from-team", "removed from the team" },
        { "r", "changed the room name to" },
        { "room_changed_topic", "changed the topic to" },
        { "room_changed_description", "changed the description to" },
        { "room_changed_announcement", "changed the announcement to" },
        { "room_changed_privacy", "changed the room type to" },
        { "room_changed_avatar", "changed the room avatar" },
        { "user-muted", "muted" },
        { "user-unmuted", "unmuted" },
        { "subscription-role-added", "was given role" },
        { "subscription-role-removed", "lost role" },
        { "message_pinned", "pinned a message" },
        { "message_pinned_e2e", "pinned a message" },
        { "discussion-created", "started a discussion" },
        { "rm", "message removed" },
        { "wm", "welcome" },
        { "videoconf", "started a call" },
        { "jitsi_call_started", "started a call" },
        { "e2e", "encrypted message" },
        { NULL, NULL }
    };
    int i;

    if (!type || !*type)
        return NULL;
    for (i = 0; tab[i].t; i++)
        if (!strcmp(tab[i].t, type))
            return tab[i].s;
    return "(system event)";
}

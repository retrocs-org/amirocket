/* Worker-owned bounded queue. Never coalesce mutating requests. */
static int command_priority(int kind)
{
    if(kind==NC_LOGOUT || kind==NC_QUIT || kind==NC_DISCONNECT || kind==NC_SET_FOCUS || kind==NC_SET_INTERVAL) return 0;
    if(kind==NC_EDIT || kind==NC_DELETE || kind==NC_REACT || kind==NC_THREAD_SEND || kind==NC_UPLOAD || kind==NC_SEND || kind==NC_CONNECT || kind==NC_LOGOUT || kind==NC_CREATE_ROOM) return 1;
    if(kind==NC_THREAD || kind==NC_SEARCH || kind==NC_OPEN_ROOM || kind==NC_HISTORY || kind==NC_IMAGE || kind==NC_DOWNLOAD) return 2;
    return 3;
}
static int duplicate_command(const NetMsg *a,const NetMsg *b)
{
    if(a->server_slot!=b->server_slot || a->server_generation!=b->server_generation || a->kind!=b->kind) return 0;
    if(a->kind==NC_POLL_NOW || a->kind==NC_PRESENCE) return 1;
    return 0;
}
/* A disconnect is a FIFO barrier. Cancel old work, but keep a newer explicit
 * CONNECT: ARexx callers can queue DISCONNECT/CONNECT before this drain runs. */
static int cancel_after_disconnect(const NetMsg *pending,const NetMsg *closing,int older)
{
    if(pending->server_slot!=closing->server_slot || command_priority(pending->kind)==0) return 0;
    if(pending->kind==NC_CONNECT && !older && pending->server_generation>=closing->server_generation) return 0;
    return 1;
}
/* Only return fixed labels: never copy server replies, URLs or credentials. */
static const char *network_diagnosis(Worker *w)
{
    char text[256];int i,status=w->traffic.last_status;
    if(status==429 || (status==503 && w->traffic.cooldown>network_now()))return "Proxy/server rate limit; waiting for cooldown";
    if(status==401 || status==403)return "Authentication or access denied";
    if(status>=500)return "Server or reverse-proxy error";
    if(status>=300 && status<400)return "Proxy redirect; check server URL";
    if(!w->net_trouble && w->sess.logged_in){
        if(w->realtime_enabled && !realtime_ready(&w->realtime))return w->realtime.ws.status>=300?"REST live; WebSocket upgrade rejected":"REST live; WebSocket retry/polling fallback";
        return "Connected";
    }
    ar_strlcpy(text,w->sess.rc.http.err,sizeof(text));for(i=0;text[i];i++)if(text[i]>='A'&&text[i]<='Z')text[i]+=32;
    if(strstr(text,"resolve") || strstr(text,"dns") || strstr(text,"host name"))return "DNS lookup failed";
    if(strstr(text,"ssl") || strstr(text,"tls") || strstr(text,"certificate"))return "TLS/certificate failure";
    if(strstr(text,"timeout") || strstr(text,"deadline"))return "Connection/read timeout";
    return w->net_trouble?"Connection unavailable; retry scheduled":"Offline or connecting";
}
static void network_snapshot(Worker *w,int queued)
{
    NetMsg *e; ar_ms now=network_now(); int cooldown=(int)((w->traffic.cooldown>now?w->traffic.cooldown-now:0)+999)/1000;
    if(w->outstanding>3) return;
    e=netmsg_new(NE_NETWORK); if(!e) return;
    snprintf(e->text,sizeof(e->text),"Server %d: %lu req/min, queue %d, cooldown %ds\n  HTTP %d, errors %lu, 429 %lu, cache %lu, realtime %s (WS HTTP %d)\n  Diagnosis: %s",w->slot+1,traffic_rpm(&w->traffic,now),queued,cooldown,w->traffic.last_status,w->traffic.errors,w->traffic.limited,w->traffic.cache_hits,!w->realtime_enabled?"off":realtime_ready(&w->realtime)?"live":w->realtime.phase?"connecting":"polling",w->realtime.ws.status,network_diagnosis(w));
    e->a_before=w->retry_due>now?(w->retry_due-now+999)/1000:0;
    e->a_interval=cooldown; e->a_verify=w->net_trouble; e->a_background=w->retry_login;
    post_event(w,e);
}

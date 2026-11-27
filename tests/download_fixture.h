/* Synthesizes a large HTTP file without allocating its body in the test. */
typedef struct {
    char header[192];size_t header_pos,position,length,truncate_at;
    void (*on_read)(void *);void *on_read_ctx;size_t notify_at;
} DownloadWire;
static int download_wire_read(void *ctx,void *data,int count)
{
    DownloadWire *w=ctx;size_t header=strlen(w->header),n;
    if(w->header_pos<header) {n=header-w->header_pos;if(n>(size_t)count)n=count;memcpy(data,w->header+w->header_pos,n);w->header_pos+=n;return n;}
    n=w->length-w->position;if(w->truncate_at && w->position>=w->truncate_at)return 0;
    if(n>(size_t)count)n=count;
    memset(data,'x',n);w->position+=n;
    if(w->on_read && w->position>=w->notify_at)w->on_read(w->on_read_ctx);
    return n;
}
static int download_wire_write(void *ctx,const void *data,int count) {(void)ctx;(void)data;return count;}
static void download_wire_close(void *ctx) {(void)ctx;}
static int download_wire_connect(void *ctx,const char *host,int port,const NetOptions *options,Transport *out,char *err,size_t size)
{
    DownloadWire *w=ctx;(void)host;(void)port;(void)options;(void)err;(void)size;
    memset(out,0,sizeof(*out));out->ctx=w;out->read=download_wire_read;out->write=download_wire_write;out->close=download_wire_close;
    w->header_pos=w->position=0;return 0;
}
static void download_wire_init(DownloadWire *w,size_t size)
{
    memset(w,0,sizeof(*w));w->length=size;
    snprintf(w->header,sizeof(w->header),"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n",(unsigned long)size);
}

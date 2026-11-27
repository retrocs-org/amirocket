/* Main-task navigation: the editor hook only queues these actions. */
static int navigation_target(int direction)
{
    int i,start=-1,n=G.side.n;
    for(i=0;i<n;i++)if(G.side.r[i].rid && G.side.r[i].server_slot==G.active_server && !strcmp(G.side.r[i].rid,G.cur_rid))start=i;
    if(start<0)start=direction>0?-1:0;
    for(i=1;i<=n;i++){int k=(start+direction*i+n*2)%n;if(G.side.r[k].rid && *G.side.r[k].rid && G.side.r[k].server_slot>=0)return k;}
    return -1;
}
static void open_navigation_index(int k)
{
    if(k<0 || k>=G.side.n || !G.side.r[k].rid)return;
    scroll_side(k);on_side_click(40,(k-G.side.top)*G.side.row_h+G.side.row_h/2);
}
static void switch_conversation(int direction){open_navigation_index(navigation_target(direction));}
static void filtered_conversation(void)
{
    int i;for(i=0;i<G.side.n;i++)if(G.side.r[i].rid && *G.side.r[i].rid){open_navigation_index(i);return;}
}
static void quick_conversation(void)
{
    if(G.win && G.g[G_FILTER]){
        G.filter[0]=0;SetGadgetAttrs((struct Gadget *)G.g[G_FILTER],G.win,NULL,STRINGA_TextVal,(ULONG)"",TAG_END);rooms_rebuild();
        ActivateLayoutGadget((struct Gadget *)G.g[0],G.win,NULL,(ULONG)G.g[G_FILTER]);
    }
}
static void transfer_caption(ar_ms bytes,ar_ms total,ar_ms started,ar_ms now,char *out,int size)
{
    ar_ms elapsed=now-started,rate=elapsed>0?bytes*1000/elapsed:0;
    if(elapsed<1000 || rate<=0){snprintf(out,size,"Transferring: %ld KiB - measuring speed...",(long)(bytes/1024));return;}
    if(total>0){long left=(long)((total>bytes?total-bytes:0)/rate);
        snprintf(out,size,"%ld / %ld KiB (%ld%%) | %ld.%ld KiB/s | %ldm %lds left",(long)(bytes/1024),(long)(total/1024),(long)(bytes*100/total),(long)(rate/1024),(long)((rate%1024)*10/1024),left/60,left%60);
    }else snprintf(out,size,"%ld KiB | %ld.%ld KiB/s | size unknown",(long)(bytes/1024),(long)(rate/1024),(long)((rate%1024)*10/1024));
}

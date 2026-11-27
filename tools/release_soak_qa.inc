/* Opt-in real production-client soak, only inside a disposable emulator profile.
 * Credentials are mounted separately at Secrets:session.json, never built in. */
#include "../src/amiga/net_amiga.h"
#include "../src/core/json.h"
static void qa_release_soak(int seconds)
{
    Prefs *saved=ar_malloc(sizeof(Prefs)),*live=ar_malloc(sizeof(Prefs));
    JDoc doc; const char *raw; char room[64],reply[2048],command[160],expected[2][256]={{0}},network[2304],err[128];
    int i,started=0,opened_clock=0,cycles=0,original_fail=qa_fail; ar_ms beginning=0,until=0,next=0; ULONG baseline=0,lowest=~0UL;
    QA(saved && live); if(!saved || !live) goto end;
    *saved=G.prefs; prefs_defaults(live); live->servers_defined=1; live->realtime=1; live->request_spacing_ms=1500;
    raw=qa_dock_read("Secrets:session.json"); QA(raw!=NULL); if(!raw) goto end;
    QA(!json_parse(&doc,raw,strlen(raw))); if(!doc.t) goto end;
    json_str(&doc,json_get(&doc,0,"url"),live->servers[0].url,sizeof(live->servers[0].url));
    json_str(&doc,json_get(&doc,0,"user_id"),live->servers[0].user_id,sizeof(live->servers[0].user_id));
    json_str(&doc,json_get(&doc,0,"token"),live->servers[0].token,sizeof(live->servers[0].token));
    json_str(&doc,json_get(&doc,0,"room_id"),room,sizeof(room)); json_free(&doc); memset(dock_log,0,sizeof(dock_log));
    QA(live->servers[0].url[0] && live->servers[0].user_id[0] && live->servers[0].token[0] && room[0]);
    if(qa_fail!=original_fail) goto end;
    live->servers[0].verify=live->servers[0].remember=live->servers[0].autoconnect=1;
    strcpy(live->servers[0].name,"Soak primary"); live->servers[1]=live->servers[0]; strcpy(live->servers[1].name,"Soak secondary");
    QA(prefs_save(live)==0);
    RexxSysBase=(struct RxsLib *)OpenLibrary("rexxsyslib.library",36); QA(RexxSysBase!=NULL); if(!RexxSysBase) goto restore;
    { BPTR script=Open("T:ARSoakClient",MODE_NEWFILE); const char *text="Stack 131072\nAmiRocket:AmiRocket >T:ARSoakClient.log\nEcho >T:ARSoakClientDone done\n";
      QA(script!=0); if(!script) goto restore; Write(script,(APTR)text,strlen(text)); Close(script);
    }
    DeleteFile("T:ARSoakClientDone"); QA(launch_helper("Execute T:ARSoakClient")>=0); started=qa_dock_port("AMIROCKET",1,1000); QA(started);
    if(!started) goto restore;
    for(i=0;i<3000;i++) {
        unsigned long connected=0;
        if(qa_dock_command("AMIROCKET","DOCKSTATUS",reply,sizeof(reply))==0 && ar_rexx_number(reply,"connected_servers",&connected) && connected==2) break;
        qa_dock_pump();
    }
    QA(i<3000); if(i==3000) goto restore;
    for(i=0;i<2;i++) {
        snprintf(command,sizeof(command),"SERVER %d",i+1); QA(!qa_dock_command("AMIROCKET",command,reply,sizeof(reply)));
        snprintf(command,sizeof(command),"OPENROOM %s",room); QA(!qa_dock_command("AMIROCKET",command,reply,sizeof(reply)));
        { int tick; for(tick=0;tick<500;tick++) qa_dock_pump(); }
        QA(!qa_dock_command("AMIROCKET","DRAFT",reply,sizeof(reply)));
        if(!reply[0]) QA(!qa_dock_command("AMIROCKET","INSERT AmiRocket stability draft - do not send",reply,sizeof(reply)));
        QA(!qa_dock_command("AMIROCKET","DRAFT",expected[i],sizeof(expected[i])));
    }
    QA(!net_amiga_open(err,sizeof(err))); opened_clock=1;
    beginning=net_amiga()->now_ms(NULL); until=beginning+(ar_ms)seconds*1000;
    qa_trace("QA production-client soak started (two authenticated server profiles)\n");
    while(net_amiga()->now_ms(NULL)<until && !G.quit && qa_fail==original_fail) {
        ar_ms t=net_amiga()->now_ms(NULL);
        if(t>=next) {
            unsigned long connected=0,iconified=0; ULONG free_now; int slot=cycles%2;
            snprintf(command,sizeof(command),"SERVER %d",slot+1); QA(!qa_dock_command("AMIROCKET",command,reply,sizeof(reply)));
            { char current[64]; QA(!qa_dock_command("AMIROCKET","STATUS",reply,sizeof(reply)));
              if(!ar_rexx_field(reply,"room",current,sizeof(current)) || strcmp(current,room)) {
                  snprintf(command,sizeof(command),"OPENROOM %s",room); QA(!qa_dock_command("AMIROCKET",command,reply,sizeof(reply)));
                  for(i=0;i<100;i++) qa_dock_pump();
              }
            }
            QA(!qa_dock_command("AMIROCKET","ICONIFY",reply,sizeof(reply)));
            QA(!qa_dock_command("AMIROCKET","DOCKSTATUS",reply,sizeof(reply)) && ar_rexx_number(reply,"iconified",&iconified) && iconified==1);
            for(i=0;i<50;i++) qa_dock_pump();
            QA(!qa_dock_command("AMIROCKET","SHOW",reply,sizeof(reply)));
            QA(!qa_dock_command("AMIROCKET","DRAFT",reply,sizeof(reply)) && !strcmp(reply,expected[slot]));
            QA(!qa_dock_command("AMIROCKET","DOCKSTATUS",reply,sizeof(reply)) && ar_rexx_number(reply,"connected_servers",&connected) && connected==2);
            { int tick,streams=0;
              for(tick=0;tick<3000;tick++) { const char *p=network; streams=0;
                  if(prefs_runtime_network(network,sizeof(network))) while((p=strstr(p,"realtime live"))) { streams++; p+=13; }
                  if(streams==2) break;
                  qa_dock_pump();
              }
              QA(streams==2);
            }
            if(cycles && cycles%5==0) {
                int tick; unsigned long online=0;
                QA(!qa_dock_command("AMIROCKET","DISCONNECT 1",reply,sizeof(reply)));
                for(tick=0;tick<500;tick++) { qa_dock_command("AMIROCKET","DOCKSTATUS",reply,sizeof(reply)); if(ar_rexx_number(reply,"connected_servers",&online) && online==1) break; qa_dock_pump(); }
                QA(tick<500);
                QA(!qa_dock_command("AMIROCKET","CONNECT 1",reply,sizeof(reply)));
                for(tick=0;tick<3000;tick++) { qa_dock_command("AMIROCKET","DOCKSTATUS",reply,sizeof(reply)); if(ar_rexx_number(reply,"connected_servers",&online) && online==2) break; qa_dock_pump(); }
                QA(tick<3000);
            }
            free_now=AvailMem(MEMF_ANY); if(free_now<lowest) lowest=free_now;
            if(t-beginning>300000 && !baseline) baseline=free_now;
            if(baseline) QA(free_now+2UL*1024*1024>=baseline);
            { char line[180]; snprintf(line,sizeof(line),"QA soak: seconds=%ld cycles=%d free=%lu baseline=%lu\n",(long)((t-beginning)/1000),cycles,free_now,baseline); qa_trace(line); }
            cycles++; next=t+60000;
        }
        qa_dock_pump();
    }
    QA(net_amiga()->now_ms(NULL)>=until); QA(cycles>=seconds/65);
    { char line[180]; snprintf(line,sizeof(line),"QA production-client soak finished: cycles=%d lowest-free=%lu\n",cycles,lowest); qa_trace(line); }
restore:
    if(opened_clock) net_amiga_close();
    if(started) {
        qa_dock_command("AMIROCKET","QUIT",reply,sizeof(reply)); QA(qa_dock_port("AMIROCKET",0,1000));
        for(i=0;i<500;i++) { BPTR marker=Open("T:ARSoakClientDone",MODE_OLDFILE); if(marker) { Close(marker); break; } qa_dock_pump(); }
        QA(i<500);
    }
    if(RexxSysBase) { CloseLibrary((struct Library *)RexxSysBase); RexxSysBase=NULL; }
    QA(prefs_save(saved)==0); DeleteFile("T:ARSoakClient"); DeleteFile("T:ARSoakClientDone");
end:
    if(live) { memset(live,0,sizeof(*live)); ar_free(live); }
    if(saved) ar_free(saved);
}

/* Isolated release Installer check. Runs only in a disposable emulator profile. */
#include <exec/types.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <intuition/intuitionbase.h>
#include <proto/intuition.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <dos/dostags.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>
#include <string.h>
struct Library *IconBase;
struct Library *CyberGfxBase;
struct IntuitionBase *IntuitionBase;
static int passed,failed;
#define QA(x) do { if(x)passed++;else {failed++;printf("FAIL %d: %s\n",__LINE__,#x);fflush(stdout);} } while(0)
static int exists(const char *path) { BPTR l=Lock((STRPTR)path,ACCESS_READ);if(!l)return 0;UnLock(l);return 1; }
static int write_text(const char *path,const char *text)
{
    BPTR f=Open((STRPTR)path,MODE_NEWFILE);int ok;if(!f)return 0;ok=Write(f,(APTR)text,strlen(text))==(LONG)strlen(text);Close(f);return ok;
}
static ULONG file_hash(const char *path)
{
    BPTR f=Open((STRPTR)path,MODE_OLDFILE);char bytes[2048];LONG n;ULONG hash=2166136261UL;int i;
    if(!f)return 0;
    while((n=Read(f,bytes,sizeof(bytes)))>0)for(i=0;i<n;i++)hash=(hash^(UBYTE)bytes[i])*16777619UL;
    Close(f);return n<0?0:hash;
}
static int run_installer(const char *script)
{
    char text[800];BPTR in,out;int i,rc;
    struct MsgPort *port=CreateMsgPort();struct IOStdReq *io=port?(struct IOStdReq *)CreateIORequest(port,sizeof(struct IOStdReq)):NULL;
    int opened=io&&OpenDevice("input.device",0,(struct IORequest *)io,0)==0;
    DeleteFile("T:ARInstallDone");DeleteFile("T:ARInstallFailed");
    snprintf(text,sizeof(text),"Stack 131072\nCD ReleaseFiles:\nSYS:System/Installer QATools:%s APPNAME AmiRocket MINUSER NOVICE DEFUSER NOVICE LOGFILE QATools:installer.log >QATools:installer-cli.log\nIf WARN\n Echo >T:ARInstallFailed failed\nEndIf\nEcho >T:ARInstallDone done\n",script);
    if(!write_text("T:ARInstallRun",text))return 0;
    in=Open("NIL:",MODE_OLDFILE);out=Open("NIL:",MODE_NEWFILE);
    if(!in||!out)return 0;
    rc=SystemTags((CONST_STRPTR)"Execute T:ARInstallRun",SYS_Asynch,TRUE,SYS_UserShell,TRUE,SYS_Input,in,SYS_Output,out,NP_StackSize,131072,TAG_END);
    if(rc<0)return 0;
    /* This runner owns the disposable screen: accept Installer's initial pages. */
    for(i=0;i<100&&!exists("T:ARInstallDone");i++) {
        Delay(25);
        if(i==10&&IntuitionBase){struct Screen *sc=IntuitionBase->FirstScreen;CyberGfxBase=OpenLibrary("cybergraphics.library",0);
          if(sc&&CyberGfxBase){int w=sc->Width,h=sc->Height;UBYTE *rgb=malloc(w*h*3);if(rgb){BPTR f;char header[80];
            ReadPixelArray(rgb,0,0,w*3,&sc->RastPort,0,0,w,h,RECTFMT_RGB);
            f=Open("QATools:screen.ppm",MODE_NEWFILE);if(f){int n=sprintf(header,"P6\n%d %d\n255\n",w,h);Write(f,header,n);Write(f,rgb,w*h*3);Close(f);}free(rgb);}
          }if(CyberGfxBase)CloseLibrary(CyberGfxBase);}

        if(opened&&(i==2 || (i>=12 && i%12==0))&&IntuitionBase){struct Window *w;
          for(w=IntuitionBase->FirstScreen->FirstWindow;w;w=w->NextWindow)if(w->Title&&(strstr((char *)w->Title,"QATools:") || strstr((char *)w->Title,"100% done")))break;
          if(w){struct InputEvent e;struct IEPointerPixel pos;struct Gadget *g,*chosen=NULL;
            for(g=w->FirstGadget;g;g=g->NextGadget){
                if(g->GadgetID==90)chosen=g;
                if(g->GadgetText && g->GadgetText->IText && !strcmp((char *)g->GadgetText->IText,"Finish")){chosen=g;break;}
            }
            /* Installer askbool uses IDs 2/1 for left/right choices. */
            if(!chosen && strstr((char *)w->Title,"100% done"))
                for(g=w->FirstGadget;g;g=g->NextGadget)if(g->GadgetID==1){chosen=g;break;}
            if(!chosen)continue;
            memset(&e,0,sizeof(e));memset(&pos,0,sizeof(pos));pos.iepp_Screen=w->WScreen;
            pos.iepp_Position.X=w->LeftEdge+chosen->LeftEdge+chosen->Width/2;
            pos.iepp_Position.Y=w->TopEdge+chosen->TopEdge+chosen->Height/2;
            e.ie_Class=IECLASS_NEWPOINTERPOS;e.ie_SubClass=IESUBCLASS_PIXEL;e.ie_EventAddress=&pos;
            io->io_Command=IND_WRITEEVENT;io->io_Data=&e;io->io_Length=sizeof(e);DoIO((struct IORequest *)io);
            memset(&e,0,sizeof(e));e.ie_Class=IECLASS_RAWMOUSE;e.ie_Code=IECODE_LBUTTON;DoIO((struct IORequest *)io);
            e.ie_Code|=IECODE_UP_PREFIX;DoIO((struct IORequest *)io);}
        }
    }
    if(opened)CloseDevice((struct IORequest *)io);
    if(io)DeleteIORequest((struct IORequest *)io);
    if(port)DeleteMsgPort(port);
    return exists("T:ARInstallDone")&&!exists("T:ARInstallFailed");
}
static int checks(void)
{
    const char *dest="RAM:AmiRocketInstallerQA/AmiRocket";ULONG prefs_hash;BPTR root,held;struct DiskObject *icon;
    { struct Library *wb=OpenLibrary("workbench.library",39);
      printf("Installer QA: Exec %d, Workbench %d\n",SysBase->LibNode.lib_Version,wb?wb->lib_Version:0); if(wb)CloseLibrary(wb); }
    puts("Installer QA: clean install / update / uninstall");fflush(stdout);
    IntuitionBase=(struct IntuitionBase *)OpenLibrary("intuition.library",39);
    root=CreateDir("RAM:AmiRocketInstallerQA");if(root)UnLock(root);
    root=CreateDir("RAM:AmiRocketInstallerQA/settings");if(root)UnLock(root);
    if(!exists("RAM:AmiRocketInstallerQA/settings/AmiRocket.prefs"))QA(write_text("RAM:AmiRocketInstallerQA/settings/AmiRocket.prefs","workspace.installer_test=keep\nplugin.private.option=keep\n"));
    QA(write_text("RAM:AmiRocketInstallerQA/settings/volatile.prefs","old fallback"));
    prefs_hash=file_hash("RAM:AmiRocketInstallerQA/settings/AmiRocket.prefs");QA(prefs_hash!=0);
    { ULONG flags=LDF_ASSIGNS|LDF_DEVICES|LDF_VOLUMES|LDF_READ;struct DosList *list=LockDosList(flags);
      QA(list!=NULL);
      if(list){QA(FindDosEntry(list,"AmiRocket",flags)==NULL);UnLockDosList(flags);}
    }
    puts("Installer QA: real Novice fresh-install flow without a drawer request");fflush(stdout);
    QA(write_text("ENVARC:AmiRocket.prefs","interval=19\nplugin.custom.option=keep\nworkspace.install_dir=MissingAmiRocketDisk:OldInstall\n"));
    QA(run_installer("NoviceFreshTest"));
    QA(exists("RAM:AmiRocketInstallerQA/Auto/AmiRocket"));
    QA(exists("RAM:AmiRocketInstallerQA/Data/Plugins/timestamp.arp"));
    QA(!exists("RAM:AmiRocketInstallerQA/Auto/Plugins"));
    QA(!exists("RAM:AmiRocketInstallerQA/Auto/ReadMe"));
    QA(exists("RAM:AmiRocketInstallerQA/Help/AmiRocket.guide"));
    QA(!exists("RAM:AmiRocketInstallerQA/Auto/AmiRocket.guide"));
    if(!run_installer("InstallTest")){puts("Initial Installer run did not complete; remaining checks skipped.");return 5;}
    QA(exists("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket"));
    QA(exists("RAM:AmiRocketInstallerQA/Prefs/AmiRocketPrefs"));
    puts("Installer QA: custom icon drawers, spaces and assign roots");fflush(stdout);
    root=CreateDir("RAM:AmiRocketInstallerQA/Custom Apps");QA(root!=0);if(root)UnLock(root);
    root=CreateDir("RAM:AmiRocketInstallerQA/Custom Prefs");QA(root!=0);if(root)UnLock(root);
    root=Lock("RAM:AmiRocketInstallerQA/Custom Apps",ACCESS_READ);QA(root && AssignLock("ARIconAppQA",root));
    root=Lock("RAM:AmiRocketInstallerQA/Custom Prefs",ACCESS_READ);QA(root && AssignLock("ARIconPrefsQA",root));
    QA(SystemTags((CONST_STRPTR)"ReleaseFiles:CreateIcons ARIconAppQA: ICONS ARIconPrefsQA:",NP_StackSize,131072,TAG_END)==0);
    QA(exists("RAM:AmiRocketInstallerQA/Custom Apps/AmiRocket.info"));
    QA(exists("RAM:AmiRocketInstallerQA/Custom Prefs/AmiRocketPrefs.info"));
    QA(SystemTags((CONST_STRPTR)"ReleaseFiles:CreateIcons \"RAM:AmiRocketInstallerQA/Custom Apps/\" ICONS \"RAM:AmiRocketInstallerQA/Custom Prefs/\"",NP_StackSize,131072,TAG_END)==0);
    AssignLock("ARIconAppQA",0);AssignLock("ARIconPrefsQA",0);
    QA(!exists("RAM:AmiRocketInstallerQA/AmiRocket/Install"));
    QA(!exists("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocketPrefs"));
    QA(file_hash("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket")==file_hash("ReleaseFiles:AmiRocket"));
    QA(file_hash("RAM:AmiRocketInstallerQA/Prefs/AmiRocketPrefs")==file_hash("ReleaseFiles:AmiRocketPrefs"));
    QA(file_hash("RAM:AmiRocketInstallerQA/Data/Plugins/timestamp.arp")==file_hash("ReleaseFiles:Plugins/timestamp.arp"));
    QA(file_hash("RAM:AmiRocketInstallerQA/settings/AmiRocket.prefs")==prefs_hash);
    QA(write_text("RAM:AmiRocketInstallerQA/Data/Plugins/Custom.arp","keep this plugin"));
    QA(write_text("RAM:AmiRocketInstallerQA/AmiRocket/user-note.txt","keep this file"));
    IconBase=OpenLibrary("icon.library",39);QA(IconBase!=NULL);
    if(IconBase) { icon=GetDiskObject("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket");QA(icon!=NULL);if(icon){icon->do_CurrentX=121;QA(PutDiskObject("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket",icon));FreeDiskObject(icon);} }
    QA(write_text("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket","old binary"));
    QA(write_text("RAM:AmiRocketInstallerQA/AmiRocket/Install","legacy installer"));
    QA(write_text("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocketPrefs","legacy preferences"));
    held=Lock((STRPTR)dest,ACCESS_READ);QA(held!=0);
    puts("Installer QA: update with a Workbench drawer lock and custom files");fflush(stdout);
    QA(run_installer("InstallTest"));
    QA(file_hash("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket")==file_hash("ReleaseFiles:AmiRocket"));
    QA(file_hash("RAM:AmiRocketInstallerQA/settings/AmiRocket.prefs")==prefs_hash);
    if(IconBase) { icon=GetDiskObject("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket");QA(icon&&icon->do_CurrentX==121);if(icon)FreeDiskObject(icon); }
    if(held)UnLock(held);
    puts("Installer QA: recorded custom location and preserved plugin settings");fflush(stdout);
    QA(write_text("ENVARC:AmiRocket.prefs","interval=19\nplugin.custom.option=keep\nnotification_sound=\n"));
    QA(SystemTags((CONST_STRPTR)"ReleaseFiles:CreateIcons RAM:AmiRocketInstallerQA/AmiRocket RECORD RAM:AmiRocketInstallerQA/Prefs",NP_StackSize,131072,TAG_END)==0);
    QA(SystemTags((CONST_STRPTR)"ReleaseFiles:CreateIcons DETECT",NP_StackSize,131072,TAG_END)==0);
    { char found[512]={0};BPTR detected=0,expected=Lock((STRPTR)dest,ACCESS_READ);
      QA(GetVar("AmiRocket-InstallDir",found,sizeof(found),GVF_GLOBAL_ONLY)>0);
      detected=Lock(found,ACCESS_READ);QA(detected&&expected&&SameLock(detected,expected)==LOCK_SAME);
      if(detected)UnLock(detected);
      if(expected)UnLock(expected);
    }
    { BPTR f=Open("ENVARC:AmiRocket.prefs",MODE_OLDFILE);char settings[32768];LONG n=f?Read(f,settings,sizeof(settings)-1):0;
      if(f)Close(f);
      settings[n>0?n:0]=0;QA(strstr(settings,"interval=19")&&strstr(settings,"plugin.custom.option=keep")&&strstr(settings,"workspace.install_dir="));
      QA(strstr(settings,"notification_sound=") && strstr(settings,"/Sounds/Notification.8svx"));
      QA(exists("RAM:AmiRocketInstallerQA/Data/Sounds/Notification.8svx"));
    }
    QA(write_text("ENVARC:AmiRocket.prefs","interval=19\nplugin.custom.option=keep\nnotification_sound=T:personal.8svx\n"));
    QA(SystemTags((CONST_STRPTR)"ReleaseFiles:CreateIcons RAM:AmiRocketInstallerQA/AmiRocket RECORD RAM:AmiRocketInstallerQA/Prefs",NP_StackSize,131072,TAG_END)==0);
    { BPTR f=Open("ENVARC:AmiRocket.prefs",MODE_OLDFILE);char settings[32768];LONG n=f?Read(f,settings,sizeof(settings)-1):0;
      if(f)Close(f);
      settings[n>0?n:0]=0;QA(strstr(settings,"notification_sound=T:personal.8svx")!=NULL);
    }
    puts("Installer QA: real Novice maintenance at the recorded custom location");fflush(stdout);
    QA(run_installer("NoviceUpdateTest"));
    QA(file_hash("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket")==file_hash("ReleaseFiles:AmiRocket"));
    puts("Installer QA: uninstall keeps settings and unrelated plugins");fflush(stdout);
    QA(run_installer("UninstallTest"));
    QA(!exists("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket"));
    QA(!exists("RAM:AmiRocketInstallerQA/Prefs/AmiRocketPrefs"));
    QA(exists("RAM:AmiRocketInstallerQA/Data/Plugins/Custom.arp"));
    QA(exists("RAM:AmiRocketInstallerQA/AmiRocket/user-note.txt"));
    QA(file_hash("RAM:AmiRocketInstallerQA/settings/AmiRocket.prefs")==prefs_hash);
    QA(exists("RAM:AmiRocketInstallerQA/settings/volatile.prefs"));
    QA(SystemTags((CONST_STRPTR)"ReleaseFiles:CreateIcons RAM:AmiRocketInstallerQA/AmiRocket RECORD RAM:AmiRocketInstallerQA/Prefs",NP_StackSize,131072,TAG_END)!=0);
    /* Reproduce a shared Utilities drawer retaining the old AmiRocket drawer. */
    QA(SystemTags((CONST_STRPTR)"ReleaseFiles:CreateIcons RAM:AmiRocketInstallerQA RECORD RAM:AmiRocketInstallerQA/Prefs",NP_StackSize,131072,TAG_END)!=0);
    puts("Installer QA: reinstall, then uninstall with settings removal");fflush(stdout);
    QA(run_installer("InstallTest"));
    QA(file_hash("RAM:AmiRocketInstallerQA/settings/AmiRocket.prefs")==prefs_hash);
    QA(run_installer("UninstallRemoveTest"));
    QA(!exists("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket"));
    QA(!exists("RAM:AmiRocketInstallerQA/Prefs/AmiRocketPrefs"));
    QA(!exists("RAM:AmiRocketInstallerQA/settings/AmiRocket.prefs"));
    QA(!exists("RAM:AmiRocketInstallerQA/settings/volatile.prefs"));
    QA(exists("RAM:AmiRocketInstallerQA/Data/Plugins/Custom.arp"));
    QA(exists("RAM:AmiRocketInstallerQA/AmiRocket/user-note.txt"));
    puts("Installer QA: expert configuration migration and split Preferences");fflush(stdout);
    root=CreateDir("RAM:AmiRocketInstallerQA/Config");if(root)UnLock(root);
    QA(write_text("ENVARC:AmiRocket.prefs","interval=23\nplugin.custom.option=single-file\n"));
    QA(SystemTags((CONST_STRPTR)"ReleaseFiles:CreateIcons RAM:AmiRocketInstallerQA/Config/AmiRocket.prefs CONFIG",NP_StackSize,131072,TAG_END)==0);
    QA(exists("RAM:AmiRocketInstallerQA/Config/AmiRocket.prefs") && !exists("ENVARC:AmiRocket.prefs"));
    QA(run_installer("InstallTest"));
    QA(SystemTags((CONST_STRPTR)"ReleaseFiles:CreateIcons RAM:AmiRocketInstallerQA/AmiRocket RECORD RAM:AmiRocketInstallerQA/Prefs",NP_StackSize,131072,TAG_END)==0);
    QA(SystemTags((CONST_STRPTR)"RAM:AmiRocketInstallerQA/Prefs/AmiRocketPrefs SELFTEST",NP_StackSize,131072,TAG_END)==0);
    QA(run_installer("CustomUninstallKeepTest"));
    QA(exists("RAM:AmiRocketInstallerQA/Config/AmiRocket.prefs") && !exists("RAM:AmiRocketInstallerQA/Prefs/AmiRocketPrefs"));
    QA(run_installer("InstallTest"));
    QA(run_installer("CustomUninstallRemoveTest"));
    QA(!exists("RAM:AmiRocketInstallerQA/Config/AmiRocket.prefs") && !exists("RAM:AmiRocketInstallerQA/Prefs/AmiRocketPrefs"));
    DeleteVar("AmiRocket-ConfigFile",GVF_GLOBAL_ONLY); DeleteFile("ENVARC:AmiRocket-ConfigFile");
    puts("Installer QA: shared-data relocation and destination validation");fflush(stdout);
    root=CreateDir("RAM:AmiRocketInstallerQA/ExpertData");if(root)UnLock(root);
    QA(write_text("ENVARC:AmiRocket.prefs","workspace.data_dir=RAM:AmiRocketInstallerQA/Data\nplugin.custom.path=RAM:AmiRocketInstallerQA/Data/Plugins/Custom.arp\nnotification_sound=RAM:AmiRocketInstallerQA/Data/Sounds/Notification.8svx\n"));
    QA(write_text("RAM:AmiRocketInstallerQA/Data/AmiRocket.drafts","preserve drafts"));
    QA(SystemTags((CONST_STRPTR)"ReleaseFiles:CreateIcons RAM:AmiRocketInstallerQA/ExpertData DATA RAM:AmiRocketInstallerQA/AmiRocket RAM:AmiRocketInstallerQA/Prefs",NP_StackSize,131072,TAG_END)==0);
    QA(exists("RAM:AmiRocketInstallerQA/ExpertData/Plugins/Custom.arp"));
    QA(file_hash("RAM:AmiRocketInstallerQA/ExpertData/AmiRocket.drafts")==file_hash("RAM:AmiRocketInstallerQA/Data/AmiRocket.drafts"));
    { BPTR f=Open("ENVARC:AmiRocket.prefs",MODE_OLDFILE);char text[32768];LONG n=f?Read(f,text,sizeof(text)-1):0;if(f)Close(f);text[n>0?n:0]=0;
      QA(strstr(text,"/ExpertData\n")!=NULL);
      QA(strstr(text,"/ExpertData/Plugins/Custom.arp")!=NULL);
    }
    QA(SystemTags((CONST_STRPTR)"ReleaseFiles:CreateIcons RAM:AmiRocketInstallerQA/AmiRocket DATA-CHECK RAM:AmiRocketInstallerQA/AmiRocket RAM:AmiRocketInstallerQA/Prefs",NP_StackSize,131072,TAG_END)!=0);
    QA(SystemTags((CONST_STRPTR)"ReleaseFiles:CreateIcons ReleaseFiles: DATA-CHECK RAM:AmiRocketInstallerQA/AmiRocket RAM:AmiRocketInstallerQA/Prefs",NP_StackSize,131072,TAG_END)!=0);
    puts("Installer QA: optional one-time client launch");fflush(stdout);
    root=CreateDir("RAM:AmiRocketInstallerQA/LaunchTest");if(root)UnLock(root);
    QA(SystemTags((CONST_STRPTR)"Copy QATools:LaunchQA TO RAM:AmiRocketInstallerQA/LaunchTest/AmiRocket",NP_StackSize,131072,TAG_END)==0);
    DeleteFile("T:AmiRocket-launch-qa.done");
    QA(SystemTags((CONST_STRPTR)"ReleaseFiles:CreateIcons RAM:AmiRocketInstallerQA/LaunchTest START",NP_StackSize,131072,TAG_END)==0);
    { int tick; for(tick=0;tick<100 && !exists("T:AmiRocket-launch-qa.done");tick++)Delay(2); QA(exists("T:AmiRocket-launch-qa.done")); }
    if(IconBase)CloseLibrary(IconBase);
    if(IntuitionBase)CloseLibrary((struct Library *)IntuitionBase);
    printf("Installer checks: %d passed, %d failed\n",passed,failed);return failed?5:0;
}
static struct Task *parent;static BYTE sig;static int result;
static void entry(void){result=checks();Forbid();Signal(parent,1UL<<sig);}
int main(void)
{
    struct Process *p;parent=FindTask(NULL);sig=AllocSignal(-1);if(sig<0)return 20;
    p=CreateNewProcTags(NP_Entry,(ULONG)entry,NP_StackSize,131072,NP_Name,(ULONG)"AmiRocket installer checks",NP_Input,Input(),NP_Output,Output(),NP_CloseInput,FALSE,NP_CloseOutput,FALSE,TAG_END);
    if(!p){FreeSignal(sig);return 20;}Wait(1UL<<sig);FreeSignal(sig);return result;
}

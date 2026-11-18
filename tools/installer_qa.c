/* Isolated release Installer check. Runs only in a disposable emulator profile. */
#include <exec/types.h>
#include <exec/execbase.h>
#include <dos/dos.h>
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
    snprintf(text,sizeof(text),"Stack 131072\nCD AmiRocket:\nSYS:System/Installer QATools:%s APPNAME AmiRocket MINUSER NOVICE DEFUSER NOVICE LOGFILE QATools:installer.log >QATools:installer-cli.log\nIf WARN\n Echo >T:ARInstallFailed failed\nEndIf\nEcho >T:ARInstallDone done\n",script);
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

        if(opened&&(i==2||i==12||i==24)&&IntuitionBase){struct Window *w;
          for(w=IntuitionBase->FirstScreen->FirstWindow;w;w=w->NextWindow)if(w->Title&&strstr((char *)w->Title,"QATools:"))break;
          if(w){struct InputEvent e;struct IEPointerPixel pos;memset(&e,0,sizeof(e));
            pos.iepp_Screen=w->WScreen;pos.iepp_Position.X=w->LeftEdge+(i==2?100:w->Width/2);pos.iepp_Position.Y=w->TopEdge+w->Height-(i==2?22:8);
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
    if(!exists("ENVARC:AmiRocket.prefs"))QA(write_text("ENVARC:AmiRocket.prefs","workspace.installer_test=keep\nplugin.private.option=keep\n"));
    prefs_hash=file_hash("ENVARC:AmiRocket.prefs");QA(prefs_hash!=0);
    if(!run_installer("InstallTest")){puts("Initial Installer run did not complete; remaining checks skipped.");return 5;}
    QA(exists("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket"));
    QA(file_hash("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket")==file_hash("AmiRocket:AmiRocket"));
    QA(file_hash("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocketPrefs")==file_hash("AmiRocket:AmiRocketPrefs"));
    QA(file_hash("RAM:AmiRocketInstallerQA/AmiRocket/Plugins/timestamp.arp")==file_hash("AmiRocket:Plugins/timestamp.arp"));
    QA(file_hash("ENVARC:AmiRocket.prefs")==prefs_hash);
    QA(write_text("RAM:AmiRocketInstallerQA/AmiRocket/Plugins/Custom.arp","keep this plugin"));
    QA(write_text("RAM:AmiRocketInstallerQA/AmiRocket/user-note.txt","keep this file"));
    IconBase=OpenLibrary("icon.library",39);QA(IconBase!=NULL);
    if(IconBase) { icon=GetDiskObject("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket");QA(icon!=NULL);if(icon){icon->do_CurrentX=121;QA(PutDiskObject("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket",icon));FreeDiskObject(icon);} }
    QA(write_text("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket","old binary"));
    held=Lock((STRPTR)dest,ACCESS_READ);QA(held!=0);
    puts("Installer QA: update with a Workbench drawer lock and custom files");fflush(stdout);
    QA(run_installer("InstallTest"));
    QA(file_hash("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket")==file_hash("AmiRocket:AmiRocket"));
    QA(file_hash("ENVARC:AmiRocket.prefs")==prefs_hash);
    if(IconBase) { icon=GetDiskObject("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket");QA(icon&&icon->do_CurrentX==121);if(icon)FreeDiskObject(icon); }
    if(held)UnLock(held);
    puts("Installer QA: uninstall keeps settings and unrelated plugins");fflush(stdout);
    QA(run_installer("UninstallTest"));
    QA(!exists("RAM:AmiRocketInstallerQA/AmiRocket/AmiRocket"));
    QA(exists("RAM:AmiRocketInstallerQA/AmiRocket/Plugins/Custom.arp"));
    QA(exists("RAM:AmiRocketInstallerQA/AmiRocket/user-note.txt"));
    QA(file_hash("ENVARC:AmiRocket.prefs")==prefs_hash);
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

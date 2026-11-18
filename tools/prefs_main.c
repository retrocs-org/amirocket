/* Standalone native preferences editor; does not start a network worker. */
#include "../src/amiga/gui.c"
#include <dos/dostags.h>
static int prefs_status, prefs_check, prefs_edit=-1;
static struct Task *prefs_parent;
static BYTE prefs_signal;
static LONG prefs_run(void)
{
    int result = RETURN_FAIL;
    if (ar_mem_init() < 0) return result;
    memset(&G, 0, sizeof(G)); G.dirty_sig = -1; G.prefs_only = 1;
    if (open_libs() < 0) goto done;
    if (!prefs_runtime_get(&G.prefs)) prefs_load(&G.prefs);
    if(prefs_check==4) {
        prefs_defaults(&G.prefs); G.prefs.servers_defined=1;
        strcpy(G.prefs.servers[0].url,"https://chat.noh.nu"); strcpy(G.prefs.servers[0].user,"example"); G.prefs.servers[0].autoconnect=1;
        strcpy(G.prefs.servers[1].url,"https://rocket.example.org"); strcpy(G.prefs.servers[1].user,"example");
    }
    G.port = CreateMsgPort();
    G.scr = LockPubScreen(NULL);
    if (G.port && G.scr) {
        if(prefs_edit>=0) result=preferences_edit_server(prefs_edit);
        else if (prefs_check) {
            PrefsPanel panel; int i;
            result = prefs_panel_build(&panel) ? RETURN_OK : RETURN_FAIL;
            if (!result) {
                if(prefs_check==4) { prefs_panel_select(&panel,6); Delay(2500); }
                else if(prefs_check==3) { prefs_panel_select(&panel,2); Delay(2000); }
                else for (i = 0; i < 7; i++) { prefs_panel_select(&panel, i); if (prefs_check == 2) Delay(350); }
                prefs_panel_dispose(&panel);
            }
        } else { preferences_window(); result = RETURN_OK; }
    }
    if (G.scr) { UnlockPubScreen(NULL, G.scr); G.scr = NULL; }
    if (G.port) { DeleteMsgPort(G.port); G.port = NULL; }
done:
    close_libs(); ar_mem_cleanup(); return result;
}
static void prefs_child(void)
{
    prefs_status = prefs_run();
    Forbid(); Signal(prefs_parent, 1UL << prefs_signal);
}
int main(int argc, char **argv)
{
    struct Process *child;
    if (argc > 1) prefs_check = !strcmp(argv[1],"SHOWSERVERS") ? 4 : !strcmp(argv[1], "SHOWPLUGINS") ? 3 : !strcmp(argv[1], "SHOW") ? 2 : !strcmp(argv[1], "SELFTEST");
    if(argc>1 && !strncmp(argv[1],"EDIT=",5)) {
        char *end; long slot=strtol(argv[1]+5,&end,10);
        if(*end || slot<1 || slot>PREFS_SERVERS_MAX) return RETURN_FAIL;
        prefs_edit=slot-1;
    }
    prefs_parent = FindTask(NULL); prefs_signal = AllocSignal(-1);
    if (prefs_signal < 0) return RETURN_FAIL;
    child = CreateNewProcTags(NP_Entry, (ULONG)prefs_child, NP_Name, (ULONG)"AmiRocket Preferences",
                             NP_StackSize, 131072, TAG_END);
    if (!child) { FreeSignal(prefs_signal); return RETURN_FAIL; }
    Wait(1UL << prefs_signal); FreeSignal(prefs_signal); return prefs_status;
}

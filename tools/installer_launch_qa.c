/* Stand-in client for the installer's optional one-time launch check. */
#include <dos/dos.h>
#include <proto/dos.h>
int main(void)
{
    BPTR file=Open("T:AmiRocket-launch-qa.done",MODE_NEWFILE);
    if(!file) return 20;
    Write(file,"started",7);Close(file);return 0;
}

/* Native viewer stand-in: record the literal local-file argument. */
#include <dos/dos.h>
#include <proto/dos.h>
#include <string.h>
int main(int argc,char **argv)
{
    BPTR f;
    if(argc!=2) return 10;
    f=Open("T:AmiRocket-viewer-qa.receipt",MODE_NEWFILE);
    if(!f) return 20;
    Write(f,argv[1],strlen(argv[1])); Close(f); return 0;
}

/* Lrom Video Resizer native app launcher.
 * LaunchServices enters a Mach-O executable, which starts the existing local
 * app bootstrap directly. It never asks Terminal.app to open a script.
 * ABI declarations use only stable macOS libSystem APIs; no SDK code is shipped.
 */
extern int _NSGetExecutablePath(char *buffer, unsigned int *size);
extern int execv(const char *path, char *const arguments[]);
extern char *realpath(const char *path, char *resolved);
extern long write(int descriptor, const void *buffer, unsigned long size);

static unsigned long length(const char *s) {
    unsigned long n=0; while(s[n]) ++n; return n;
}
static int failure(const char *message) {
    (void)write(2,message,length(message)); return 127;
}
int main(void) {
    char executable[4096], resolved[4096], script[4096];
    unsigned int capacity=sizeof(executable);
    if(_NSGetExecutablePath(executable,&capacity)!=0)
        return failure("Lrom launcher: application path is too long.\n");
    const char *path=realpath(executable,resolved);
    if(!path) path=executable;
    unsigned long n=length(path), slash=n;
    while(slash>0 && path[slash-1]!='/') --slash;
    if(slash==0) return failure("Lrom launcher: unable to locate app resources.\n");
    const char suffix[]="../Resources/launch.sh";
    if(slash+sizeof(suffix)>sizeof(script))
        return failure("Lrom launcher: resource path is too long.\n");
    for(unsigned long i=0;i<slash;++i) script[i]=path[i];
    for(unsigned long i=0;i<sizeof(suffix);++i) script[slash+i]=suffix[i];
    char *arguments[]={"/bin/bash",script,0};
    execv("/bin/bash",arguments);
    return failure("Lrom launcher: unable to start local application.\n");
}

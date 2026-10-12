"""Exercise the production C launcher path/exec chain using a Linux path API shim."""
from pathlib import Path
import tempfile,subprocess,json,shutil

SOURCE=Path(__file__).with_name('launcher.c')
SHIM='''#include <unistd.h>\nint _NSGetExecutablePath(char *b,unsigned int *size){ssize_t n=readlink("/proc/self/exe",b,*size-1);if(n<0 || n>=*size-1)return -1;b[n]=0;return 0;}\n'''
results=[]
with tempfile.TemporaryDirectory(prefix='lrom-native-') as tmp:
    root=Path(tmp)
    shim=root/'shim.c';shim.write_text(SHIM)
    binary=root/'launcher'
    subprocess.run(['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-O2','-Wall','-Wextra','-Werror',str(SOURCE),str(shim),'-o',str(binary)],check=True)
    for name in ['普通中文软件.app',"Renamed O'Brien App.app",'A much longer path with spaces 软件.app']:
        bundle=root/name
        exe=bundle/'Contents/MacOS/lrom-launcher'
        resource=bundle/'Contents/Resources'
        exe.parent.mkdir(parents=True);resource.mkdir(parents=True)
        shutil.copy2(binary,exe)
        script=resource/'launch.sh'
        # The script has no executable bit. Bash must read it directly.
        script.write_text('#!/bin/bash\nset -eu\nprintf "NATIVE_LAUNCH_OK\\n"\n')
        script.chmod(0o644)
        result=subprocess.run([str(exe)],cwd='/',capture_output=True,text=True,check=True)
        assert result.stdout=='NATIVE_LAUNCH_OK\n',result
        results.append(name)
print(json.dumps({'native_launch_chain_cases':len(results),'passed':results,'platform':'Linux with executable-path shim; Finder and macOS runtime not executed'},ensure_ascii=False,indent=2))

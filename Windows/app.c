#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include <wctype.h>
#include "video_core.h"

#define PATHCAP 4096
#define CMDCAP 32768
#define MAXFILES 5000
#define IDC_ADD 101
#define IDC_FOLDER 102
#define IDC_CLEAR 103
#define IDC_OUTPUT 104
#define IDC_START 105
#define IDC_STOP 106
#define IDC_OPEN 107
#define IDC_PRESET 108
#define IDC_TIPS 109
#define WM_BEGIN_BATCH (WM_APP+1)
enum { PH_IDLE,PH_INSTALL,PH_PROBE,PH_CONVERT,PH_VERIFY };
typedef struct { wchar_t path[PATHCAP]; } Entry;
static HINSTANCE instance;
static HWND window,filesView,logView,statusView,progressView,outputView,presetView,modeView,qualityView,widthView,heightView;
static HWND buttons[9];static HFONT font,headingFont;
static Entry *files;static int fileCount=0,cursor=0,successes=0,failures=0,running=0,cancelled=0,phase=PH_IDLE,targetW=1080,targetH=1920,mode=0,crf=20;
static VideoInfo currentInfo;
static wchar_t cacheDir[PATHCAP],enginePath[PATHCAP],outputDir[PATHCAP],progressPath[PATHCAP],errorPath[PATHCAP],partialPath[PATHCAP],scriptPath[PATHCAP],reportPath[PATHCAP];
static HANDLE process=NULL,jobObject=NULL,report=INVALID_HANDLE_VALUE;
static double scaleFactor=1.0;
static int automation=0,exitAfterBatch=0,lastExit=0;

static int px(int n){return (int)(n*scaleFactor+0.5);}
static int file_exists(const wchar_t *p){DWORD a=GetFileAttributesW(p);return a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY);}
static const wchar_t *basename(const wchar_t *p){const wchar_t *a=wcsrchr(p,L'\\'),*b=wcsrchr(p,L'/');if(b&&(!a||b>a))a=b;return a?a+1:p;}
static void copy_path(wchar_t *dst,const wchar_t *src){wcsncpy(dst,src,PATHCAP-1);dst[PATHCAP-1]=0;}
static int join_path(wchar_t *out,const wchar_t *a,const wchar_t *b){int n=swprintf(out,PATHCAP,L"%ls\\%ls",a,b);return n>=0&&n<PATHCAP;}
static void notify(const wchar_t *title,const wchar_t *text){if(!automation)MessageBoxW(window,text,title,MB_OK|MB_ICONINFORMATION);}
static void set_status(const wchar_t *s){if(statusView)SetWindowTextW(statusView,s);}
static void write_utf8(HANDLE h,const wchar_t *s){int n=WideCharToMultiByte(CP_UTF8,0,s,-1,NULL,0,NULL,NULL);if(n<=1)return;char *b=(char*)malloc((size_t)n);if(!b)return;WideCharToMultiByte(CP_UTF8,0,s,-1,b,n,NULL,NULL);DWORD done;WriteFile(h,b,(DWORD)(n-1),&done,NULL);free(b);}
static void log_message(const wchar_t *fmt,...){
    wchar_t text[6000];va_list args;va_start(args,fmt);_vsnwprintf(text,5990,fmt,args);va_end(args);text[5990]=0;wcscat(text,L"\r\n");
    if(logView){int len=GetWindowTextLengthW(logView);if(len>80000)SetWindowTextW(logView,L"（完整记录见导出目录中的处理报告）\r\n");SendMessageW(logView,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);SendMessageW(logView,EM_REPLACESEL,FALSE,(LPARAM)text);}
    if(report!=INVALID_HANDLE_VALUE)write_utf8(report,text);
}
static wchar_t *wide_utf8(const char *s){int n=MultiByteToWideChar(CP_UTF8,0,s,-1,NULL,0);if(!n)return NULL;wchar_t *out=(wchar_t*)calloc((size_t)n,sizeof(wchar_t));if(out)MultiByteToWideChar(CP_UTF8,0,s,-1,out,n);return out;}
static char *read_text(const wchar_t *p){
    HANDLE h=CreateFileW(p,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);if(h==INVALID_HANDLE_VALUE)return (char*)calloc(1,1);
    LARGE_INTEGER size;if(!GetFileSizeEx(h,&size)){CloseHandle(h);return (char*)calloc(1,1);}DWORD bytes=(DWORD)(size.QuadPart>1024*1024?1024*1024:size.QuadPart);
    if(size.QuadPart>bytes){LARGE_INTEGER offset;offset.QuadPart=size.QuadPart-bytes;SetFilePointerEx(h,offset,NULL,FILE_BEGIN);}
    char *out=(char*)calloc((size_t)bytes+1,1);DWORD got=0;if(out)ReadFile(h,out,bytes,&got,NULL);CloseHandle(h);return out;
}
static void set_row(int row,int column,const wchar_t *value){if(filesView)ListView_SetItemText(filesView,row,column,(wchar_t*)value);}
static void busy(int value){for(int i=0;i<6;++i)if(buttons[i])EnableWindow(buttons[i],!value);if(buttons[6])EnableWindow(buttons[6],value);EnableWindow(presetView,!value);EnableWindow(modeView,!value);EnableWindow(qualityView,!value);EnableWindow(widthView,!value);EnableWindow(heightView,!value);}
static void refresh_output(void){if(outputView)SetWindowTextW(outputView,outputDir[0]?outputDir:L"默认在第一条原视频旁新建“尺寸转换结果”文件夹");}
static int supported(const wchar_t *p){const wchar_t *e=wcsrchr(p,L'.');if(!e)return 0;const wchar_t *types[]={L".mp4",L".mov",L".m4v",L".mkv",L".avi",L".webm",L".mts",L".m2ts",L".ts"};for(int i=0;i<9;++i)if(!_wcsicmp(e,types[i]))return 1;return 0;}
static void add_file(const wchar_t *p){
    if(running||fileCount>=MAXFILES||wcslen(p)>=PATHCAP||!supported(p)||!file_exists(p))return;
    for(int i=0;i<fileCount;++i)if(!_wcsicmp(files[i].path,p))return;
    copy_path(files[fileCount].path,p);
    if(filesView){LVITEMW item={0};item.mask=LVIF_TEXT;item.iItem=fileCount;item.pszText=(wchar_t*)basename(p);ListView_InsertItem(filesView,&item);set_row(fileCount,1,L"待处理");set_row(fileCount,2,L"—");set_row(fileCount,3,L"—");}
    ++fileCount;
    if(!outputDir[0]){wchar_t parent[PATHCAP];copy_path(parent,p);wchar_t *last=(wchar_t*)basename(parent);if(last>parent)last[-1]=0;else copy_path(parent,L".");join_path(outputDir,parent,L"尺寸转换结果");refresh_output();}
    wchar_t status[120];swprintf(status,120,L"已添加 %d 个视频 · 可继续多选或拖入",fileCount);set_status(status);
}
static void add_path(const wchar_t *p){DWORD attr=GetFileAttributesW(p);if(attr==INVALID_FILE_ATTRIBUTES)return;if(!(attr&FILE_ATTRIBUTE_DIRECTORY)){add_file(p);return;}wchar_t pattern[PATHCAP];if(!join_path(pattern,p,L"*"))return;WIN32_FIND_DATAW data;HANDLE h=FindFirstFileW(pattern,&data);if(h==INVALID_HANDLE_VALUE)return;do{if(!(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_HIDDEN))){wchar_t child[PATHCAP];if(join_path(child,p,data.cFileName))add_file(child);}}while(FindNextFileW(h,&data));FindClose(h);}
static void choose_files(void){
    wchar_t *buffer=(wchar_t*)calloc(65536,sizeof(wchar_t));if(!buffer)return;OPENFILENAMEW o={0};o.lStructSize=sizeof(o);o.hwndOwner=window;o.lpstrFile=buffer;o.nMaxFile=65536;o.lpstrTitle=L"添加视频（可多选）";o.lpstrFilter=L"视频文件\0*.mp4;*.mov;*.m4v;*.mkv;*.avi;*.webm;*.mts;*.m2ts;*.ts\0所有文件\0*.*\0\0";o.Flags=OFN_EXPLORER|OFN_ALLOWMULTISELECT|OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&o)){wchar_t *next=buffer+wcslen(buffer)+1;if(!*next)add_file(buffer);else while(*next){wchar_t full[PATHCAP];if(join_path(full,buffer,next))add_file(full);next+=wcslen(next)+1;}}
    else if(CommDlgExtendedError())notify(L"选择文件失败",L"一次选择的文件太多或路径过长，请分批添加。文件夹也可以直接拖入窗口。");free(buffer);
}
static int folder_dialog(wchar_t *out,const wchar_t *title){BROWSEINFOW b={0};b.hwndOwner=window;b.lpszTitle=title;b.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;PIDLIST_ABSOLUTE id=SHBrowseForFolderW(&b);if(!id)return 0;wchar_t temp[MAX_PATH];BOOL okay=SHGetPathFromIDListW(id,temp);CoTaskMemFree(id);if(okay)copy_path(out,temp);return okay;}
static void close_process(void){if(process){CloseHandle(process);process=NULL;}if(jobObject){CloseHandle(jobObject);jobObject=NULL;}}
static void delete_temps(void){if(scriptPath[0]){wchar_t tmp[PATHCAP];join_path(tmp,cacheDir,L".engine-download.whl");DeleteFileW(tmp);join_path(tmp,cacheDir,L".ffmpeg-setup.exe");DeleteFileW(tmp);}if(progressPath[0])DeleteFileW(progressPath);if(errorPath[0])DeleteFileW(errorPath);if(partialPath[0])DeleteFileW(partialPath);if(scriptPath[0])DeleteFileW(scriptPath);progressPath[0]=errorPath[0]=partialPath[0]=scriptPath[0]=0;}
static void temp_paths(void){wchar_t name[128];swprintf(name,128,L"run-%lu-%llu.progress",GetCurrentProcessId(),(unsigned long long)GetTickCount64());join_path(progressPath,cacheDir,name);swprintf(name,128,L"run-%lu-%llu.log",GetCurrentProcessId(),(unsigned long long)GetTickCount64());join_path(errorPath,cacheDir,name);}
static int spawn_command(const wchar_t *exe,wchar_t *cmd){
    close_process();SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};HANDLE out=CreateFileW(progressPath,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);HANDLE err=CreateFileW(errorPath,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(out==INVALID_HANDLE_VALUE||err==INVALID_HANDLE_VALUE||input==INVALID_HANDLE_VALUE){if(out!=INVALID_HANDLE_VALUE)CloseHandle(out);if(err!=INVALID_HANDLE_VALUE)CloseHandle(err);if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);return 0;}
    STARTUPINFOW si={0};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;si.hStdOutput=out;si.hStdError=err;si.hStdInput=input;PROCESS_INFORMATION pi={0};
    jobObject=CreateJobObjectW(NULL,NULL);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;if(jobObject)SetInformationJobObject(jobObject,JobObjectExtendedLimitInformation,&limits,sizeof(limits));
    BOOL okay=CreateProcessW(exe,cmd,NULL,NULL,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,NULL,NULL,&si,&pi);CloseHandle(out);CloseHandle(err);CloseHandle(input);
    if(!okay){close_process();return 0;}process=pi.hProcess;if(!jobObject||!AssignProcessToJobObject(jobObject,process)){TerminateProcess(process,1);CloseHandle(pi.hThread);close_process();return 0;}ResumeThread(pi.hThread);CloseHandle(pi.hThread);return 1;
}
static int spawn_args(const wchar_t *exe,const wchar_t **args,int count){wchar_t *cmd=(wchar_t*)calloc(CMDCAP,sizeof(wchar_t));if(!cmd)return 0;int okay=append_argument(cmd,CMDCAP,exe);for(int i=0;i<count&&okay;++i)okay=append_argument(cmd,CMDCAP,args[i]);if(okay)okay=spawn_command(exe,cmd);free(cmd);return okay;}
static int unique_destination(wchar_t *out){wchar_t name[PATHCAP];copy_path(name,basename(files[cursor].path));wchar_t *dot=wcsrchr(name,L'.');if(dot)*dot=0;const wchar_t *label=mode==0?L"完整画面":mode==1?L"模糊补边":L"居中裁切";for(int i=1;i<100000;++i){int n=i==1?swprintf(out,PATHCAP,L"%ls\\%ls_%dx%d_%ls.mp4",outputDir,name,targetW,targetH,label):swprintf(out,PATHCAP,L"%ls\\%ls_%dx%d_%ls_%d.mp4",outputDir,name,targetW,targetH,label,i);if(n<0||n>=PATHCAP)return 0;if(GetFileAttributesW(out)==INVALID_FILE_ATTRIBUTES)return 1;}return 0;}
static void finish_batch(void){
    running=0;phase=PH_IDLE;busy(0);if(!cancelled&&progressView)SendMessageW(progressView,PBM_SETPOS,100,0);wchar_t text[256];swprintf(text,256,L"%ls：成功 %d，失败 %d%ls",cancelled?L"已停止":L"批量完成",successes,failures,cancelled?L"；剩余任务未处理":L"");set_status(text);log_message(L"%ls",text);if(report!=INVALID_HANDLE_VALUE){CloseHandle(report);report=INVALID_HANDLE_VALUE;}delete_temps();lastExit=failures?1:cancelled?2:0;if(exitAfterBatch)PostMessageW(window,WM_CLOSE,0,0);
}
static void fail_current(const wchar_t *reason){log_message(L"失败：%ls — %ls",basename(files[cursor].path),reason);set_row(cursor,1,L"失败");set_row(cursor,3,L"见处理报告");++failures;++cursor;delete_temps();phase=PH_IDLE;}
static int install_engine(void){
    HRSRC r=FindResourceW(instance,MAKEINTRESOURCEW(201),RT_RCDATA);if(!r)return 0;HGLOBAL loaded=LoadResource(instance,r);const void *data=LockResource(loaded);DWORD size=SizeofResource(instance,r);join_path(scriptPath,cacheDir,L"prepare-engine.ps1");HANDLE h=CreateFileW(scriptPath,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);if(h==INVALID_HANDLE_VALUE)return 0;DWORD done;BOOL wrote=WriteFile(h,data,size,&done,NULL);CloseHandle(h);if(!wrote||done!=size)return 0;
    wchar_t sys[PATHCAP],ps[PATHCAP];GetSystemDirectoryW(sys,PATHCAP);join_path(ps,sys,L"WindowsPowerShell\\v1.0\\powershell.exe");const wchar_t *args[]={L"-NoProfile",L"-NonInteractive",L"-ExecutionPolicy",L"Bypass",L"-File",scriptPath,L"-CacheDir",cacheDir};temp_paths();phase=PH_INSTALL;set_status(L"首次准备转码引擎（约 31MB），请保持联网…");log_message(L"首次自动下载并校验转码引擎；准备完成后，这台电脑可离线使用。");return spawn_args(ps,args,8);
}
static void start_probe(void){temp_paths();const wchar_t *args[]={L"-hide_banner",L"-nostdin",L"-i",files[cursor].path};phase=PH_PROBE;set_row(cursor,1,L"检测素材");set_status(L"正在读取视频信息…");if(!spawn_args(enginePath,args,4))fail_current(L"无法启动转码引擎或创建任务文件。");}
static void start_convert(void){
    char graph[8192];if(!filter_graph(graph,sizeof(graph),targetW,targetH,mode,currentInfo.hdr)){fail_current(L"滤镜参数无效。");return;}wchar_t *wideGraph=wide_utf8(graph);if(!wideGraph){fail_current(L"无法构建转码参数。");return;}wchar_t token[100],crfText[16];swprintf(token,100,L".lrom-%lu-%llu.partial.mp4",GetCurrentProcessId(),(unsigned long long)GetTickCount64());if(!join_path(partialPath,outputDir,token)){free(wideGraph);fail_current(L"导出路径过长。");return;}swprintf(crfText,16,L"%d",crf);
    const wchar_t *args[]={L"-hide_banner",L"-nostdin",L"-n",L"-i",files[cursor].path,L"-filter_complex",wideGraph,L"-map",L"[v]",L"-map",L"0:a:0?",L"-c:v",L"libx264",L"-preset",L"fast",L"-crf",crfText,L"-pix_fmt",L"yuv420p",L"-threads",L"4",L"-filter_complex_threads",L"2",L"-c:a",L"aac",L"-b:a",L"192k",L"-ar",L"48000",L"-movflags",L"+faststart",L"-map_metadata",L"-1",L"-metadata:s:v:0",L"rotate=0",L"-color_primaries",L"bt709",L"-color_trc",L"bt709",L"-colorspace",L"bt709",L"-progress",L"pipe:1",L"-nostats",L"-f",L"mp4",partialPath};
    // Only tag BT.709 after explicit HDR tone mapping; preserve SDR color metadata behavior.
    const wchar_t *actual[64];int count=0;for(int i=0;i<(int)(sizeof(args)/sizeof(args[0]));++i){if(!currentInfo.hdr&&i>=35&&i<=40)continue;actual[count++]=args[i];}
    phase=PH_CONVERT;set_row(cursor,1,L"正在转码");set_row(cursor,3,L"0%");log_message(L"[%d/%d] %ls · %dx%d%ls%ls",cursor+1,fileCount,basename(files[cursor].path),currentInfo.width,currentInfo.height,currentInfo.rotated?L"（含旋转标记）":L"",currentInfo.hdr?L" · HDR 转 SDR":L"");int okay=spawn_args(enginePath,actual,count);free(wideGraph);if(!okay)fail_current(L"无法启动转码任务。");
}
static void start_verify(void){const wchar_t *args[]={L"-hide_banner",L"-nostdin",L"-i",partialPath};phase=PH_VERIFY;set_row(cursor,1,L"校验结果");if(!spawn_args(enginePath,args,4))fail_current(L"无法校验导出结果。");}
static void poll(void){
    if(!running)return;
    if(phase==PH_IDLE){if(cancelled||cursor>=fileCount){finish_batch();return;}start_probe();return;}
    if(!process){if(phase==PH_INSTALL){log_message(L"引擎准备无法启动，请检查 Windows PowerShell 是否可用。");++failures;finish_batch();}return;}
    DWORD code=STILL_ACTIVE;GetExitCodeProcess(process,&code);
    if(code==STILL_ACTIVE){if(phase==PH_CONVERT){char *s=read_text(progressPath);double p=s?progress_percent(s,currentInfo.duration):0;free(s);wchar_t text[PATHCAP+120],percentage[32];swprintf(percentage,32,L"%d%%",(int)p);set_row(cursor,3,percentage);swprintf(text,PATHCAP+120,L"正在处理 %d/%d · %d%% · %ls",cursor+1,fileCount,(int)p,basename(files[cursor].path));set_status(text);if(progressView)SendMessageW(progressView,PBM_SETPOS,(int)((cursor+p/100)/fileCount*100),0);}return;}
    close_process();if(cancelled){if(phase!=PH_INSTALL){set_row(cursor,1,L"已停止");log_message(L"停止：%ls",basename(files[cursor].path));++cursor;}else{wchar_t temp[PATHCAP];join_path(temp,cacheDir,L".engine-download.whl");DeleteFileW(temp);join_path(temp,cacheDir,L".ffmpeg-setup.exe");DeleteFileW(temp);}delete_temps();finish_batch();return;}
    char *error=read_text(errorPath);if(!error)error=(char*)calloc(1,1);
    if(phase==PH_INSTALL){if(code||!file_exists(enginePath)){char *out=read_text(progressPath);wchar_t *detail=wide_utf8(out&&*out?out:error);log_message(L"引擎准备失败：%ls",detail?detail:L"未知错误");free(detail);free(out);++failures;delete_temps();finish_batch();set_status(L"引擎准备失败，请检查网络后重新开始");}else{log_message(L"引擎已准备完成，开始处理素材。");delete_temps();phase=PH_IDLE;}}
    else if(phase==PH_PROBE){if(parse_video(error,&currentInfo)){wchar_t dimensions[100];swprintf(dimensions,100,L"%d×%d%ls",currentInfo.width,currentInfo.height,currentInfo.rotated?L" ↻":L"");set_row(cursor,2,dimensions);start_convert();}else fail_current(L"未识别到有效视频轨道，文件可能损坏或不可读。");}
    else if(phase==PH_CONVERT){if(code==0&&file_exists(partialPath))start_verify();else{size_t length=strlen(error);const char *tail=length>1600?error+length-1600:error;wchar_t *detail=wide_utf8(tail);fail_current(detail&&*detail?detail:L"转码失败，检查视频格式、空间或文件权限。");free(detail);}}
    else if(phase==PH_VERIFY){VideoInfo info;wchar_t destination[PATHCAP];if(!parse_video(error,&info)||info.width!=targetW||info.height!=targetH||info.duration<=0)fail_current(L"导出校验失败，结果的尺寸或时长异常。");else if(!unique_destination(destination)||!MoveFileExW(partialPath,destination,0))fail_current(L"无法保存完成结果，可能空间不足、路径过长或目录无权限。");else{log_message(L"完成：%ls",basename(destination));set_row(cursor,1,L"完成");set_row(cursor,3,L"100%");++successes;++cursor;delete_temps();phase=PH_IDLE;}}
    free(error);
}
static void start_batch(void){
    if(running)return;if(!fileCount){notify(L"先添加视频",L"可多选视频，或将视频/文件夹拖入窗口。");return;}
    if(!automation){wchar_t s[32];GetWindowTextW(widthView,s,32);wchar_t *end;long w=wcstol(s,&end,10);if(*end){notify(L"尺寸无效",L"宽高须为 64–4096 范围内的偶数。");return;}GetWindowTextW(heightView,s,32);long h=wcstol(s,&end,10);if(*end||!dimensions_valid((int)w,(int)h)){notify(L"尺寸无效",L"宽高须为 64–4096 范围内的偶数。");return;}targetW=(int)w;targetH=(int)h;mode=(int)SendMessageW(modeView,CB_GETCURSEL,0,0);int q=(int)SendMessageW(qualityView,CB_GETCURSEL,0,0);crf=q==1?18:q==2?23:20;}
    int dir=SHCreateDirectoryExW(window,outputDir,NULL);if(dir!=ERROR_SUCCESS&&dir!=ERROR_ALREADY_EXISTS&&dir!=ERROR_FILE_EXISTS){notify(L"无法创建导出目录",L"请选择有写入权限的导出目录。");return;}
    SYSTEMTIME now;GetLocalTime(&now);wchar_t name[180];swprintf(name,180,L"处理报告_%04d%02d%02d_%02d%02d%02d_%lu_%llu.txt",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId(),(unsigned long long)GetTickCount64());join_path(reportPath,outputDir,name);report=CreateFileW(reportPath,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);if(report==INVALID_HANDLE_VALUE){notify(L"导出目录不可写",L"请选择有写入权限的目录，或检查磁盘空间。");return;}DWORD done;WriteFile(report,"\xef\xbb\xbf",3,&done,NULL);
    if(logView)SetWindowTextW(logView,L"");cursor=successes=failures=cancelled=0;running=1;phase=PH_IDLE;busy(1);if(progressView)SendMessageW(progressView,PBM_SETPOS,0,0);for(int i=0;i<fileCount;++i){set_row(i,1,L"待处理");set_row(i,3,L"—");}log_message(L"咸鱼尺寸助手 Windows v1.0.1 · %dx%d · %ls · MP4 / H.264 / 30fps",targetW,targetH,mode==0?L"保留画面黑边":mode==1?L"保留画面模糊背景":L"居中裁切");if(!file_exists(enginePath)&&!install_engine()){log_message(L"无法准备转码引擎，首次使用需要 Windows PowerShell 和网络连接。");++failures;finish_batch();}
}
static void stop_batch(void){if(!running)return;cancelled=1;if(buttons[6])EnableWindow(buttons[6],FALSE);if(jobObject)TerminateJobObject(jobObject,ERROR_CANCELLED);else if(process)TerminateProcess(process,ERROR_CANCELLED);set_status(L"正在停止，保留已完成的视频…");}
static void show_help(void){notify(L"使用提示",L"默认 1080×1920，完整画面补黑边。\n\n模糊补边：主体完整，空白处由模糊背景填满。\n居中裁切：会裁掉两侧或上下内容，先试一条检查字幕、人物和产品。\n\n原视频不会覆盖；结果保存到导出目录。\n无音轨视频保持无声；只取首条视频和音轨，软字幕/外挂字幕不写入。HDR 尝试转为 SDR。\n\n首次联网自动准备约 31MB 引擎，之后离线使用。\n尺寸修复不等于保证通过平台所有审核；放大低清视频不能恢复细节。");}
static HWND control(const wchar_t *klass,const wchar_t *text,DWORD style,int x,int y,int w,int h,int id){HWND c=CreateWindowExW(0,klass,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),window,(HMENU)(INT_PTR)id,instance,NULL);SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;}
static HWND button(const wchar_t *text,int x,int y,int w,int id){return control(L"BUTTON",text,BS_PUSHBUTTON|WS_TABSTOP,x,y,w,30,id);}
static HWND text(const wchar_t *s,int x,int y,int w,int h){return control(L"STATIC",s,0,x,y,w,h,0);}
static HWND combo(int x,int y,int w,const wchar_t **items,int count,int id){HWND c=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,x,y,w,230,id);for(int i=0;i<count;++i)SendMessageW(c,CB_ADDSTRING,0,(LPARAM)items[i]);SendMessageW(c,CB_SETCURSEL,0,0);return c;}
static void create_ui(void){
    HWND heading=text(L"批量改尺寸，一次导出",24,18,710,38);SendMessageW(heading,WM_SETFONT,(WPARAM)headingFont,TRUE);text(L"本地处理 · 原素材保留 · 首次自动准备引擎，之后离线使用",26,62,770,24);button(L"使用提示",794,26,100,IDC_TIPS);
    buttons[0]=button(L"添加视频",24,100,118,IDC_ADD);buttons[1]=button(L"添加文件夹",152,100,132,IDC_FOLDER);buttons[2]=button(L"清空列表",294,100,112,IDC_CLEAR);
    text(L"目标尺寸",26,147,80,24);const wchar_t *presets[]={L"竖屏 9:16 · 1080×1920",L"竖屏 9:16 · 720×1280",L"横屏 16:9 · 1920×1080",L"方形 1:1 · 1080×1080",L"竖图 4:5 · 1080×1350"};presetView=combo(110,143,268,presets,5,IDC_PRESET);text(L"宽",402,147,26,24);widthView=control(L"EDIT",L"1080",WS_BORDER|ES_NUMBER|WS_TABSTOP,430,145,76,26,0);text(L"× 高",520,147,46,24);heightView=control(L"EDIT",L"1920",WS_BORDER|ES_NUMBER|WS_TABSTOP,568,145,76,26,0);
    text(L"画面处理",26,189,84,24);const wchar_t *modes[]={L"保留完整画面 · 黑边",L"保留完整画面 · 模糊背景",L"居中裁切 · 铺满（会裁内容）"};modeView=combo(110,185,315,modes,3,0);text(L"画质",455,189,48,24);const wchar_t *quality[]={L"标准清晰",L"更清晰（文件更大）",L"较小文件"};qualityView=combo(506,185,210,quality,3,0);text(L"建议先试 1 条；字幕或产品贴边时，选择保留画面。自定义宽高须为偶数。",26,224,865,24);
    text(L"导出目录",26,266,84,24);outputView=control(L"EDIT",L"",ES_READONLY|ES_AUTOHSCROLL|WS_BORDER,110,260,628,28,0);buttons[3]=button(L"选择目录",753,259,138,IDC_OUTPUT);refresh_output();
    filesView=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SHOWSELALWAYS|WS_BORDER,26,305,865,185,0);ListView_SetExtendedListViewStyle(filesView,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);const wchar_t *columns[]={L"文件名",L"状态",L"原始尺寸",L"进度"};int widths[]={450,135,150,110};for(int i=0;i<4;++i){LVCOLUMNW col={0};col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=(wchar_t*)columns[i];col.cx=px(widths[i]);ListView_InsertColumn(filesView,i,&col);}
    logView=control(L"EDIT",L"把视频或文件夹拖进窗口，或者点击“添加视频”。",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER,26,503,865,68,0);SendMessageW(logView,EM_SETLIMITTEXT,100000,0);progressView=control(PROGRESS_CLASSW,L"",0,26,585,865,14,0);SendMessageW(progressView,PBM_SETRANGE,0,MAKELPARAM(0,100));statusView=text(L"就绪 · 支持 MP4 / MOV / MKV 等常见视频",26,607,865,24);
    buttons[4]=button(L"开始批量导出",24,647,174,IDC_START);buttons[6]=button(L"停止处理",208,647,120,IDC_STOP);EnableWindow(buttons[6],FALSE);buttons[7]=button(L"打开导出目录",721,647,170,IDC_OPEN);
}
static LRESULT CALLBACK window_proc(HWND h,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_CREATE:window=h;if(!automation)create_ui();SetTimer(h,1,150,NULL);DragAcceptFiles(h,TRUE);return 0;
    case WM_TIMER:poll();return 0;
    case WM_BEGIN_BATCH:start_batch();if(!running&&exitAfterBatch){lastExit=1;PostMessageW(h,WM_CLOSE,0,0);}return 0;
    case WM_DROPFILES:if(!running){HDROP drop=(HDROP)w;UINT count=DragQueryFileW(drop,0xffffffff,NULL,0);for(UINT i=0;i<count;++i){wchar_t path[PATHCAP];if(DragQueryFileW(drop,i,path,PATHCAP)<PATHCAP)add_path(path);}DragFinish(drop);}else DragFinish((HDROP)w);return 0;
    case WM_COMMAND:if(HIWORD(w)==CBN_SELCHANGE&&LOWORD(w)==IDC_PRESET){int d[][2]={{1080,1920},{720,1280},{1920,1080},{1080,1080},{1080,1350}};int selected=(int)SendMessageW(presetView,CB_GETCURSEL,0,0);if(selected>=0&&selected<5){wchar_t value[16];swprintf(value,16,L"%d",d[selected][0]);SetWindowTextW(widthView,value);swprintf(value,16,L"%d",d[selected][1]);SetWindowTextW(heightView,value);}return 0;}
        switch(LOWORD(w)){case IDC_ADD:if(!running)choose_files();break;case IDC_FOLDER:if(!running){wchar_t p[PATHCAP];if(folder_dialog(p,L"选择素材文件夹（只读取第一层视频）"))add_path(p);}break;case IDC_CLEAR:if(!running){fileCount=0;ListView_DeleteAllItems(filesView);SetWindowTextW(logView,L"");set_status(L"列表已清空");}break;case IDC_OUTPUT:if(!running){wchar_t p[PATHCAP];if(folder_dialog(p,L"选择导出目录")){copy_path(outputDir,p);refresh_output();}}break;case IDC_START:start_batch();break;case IDC_STOP:stop_batch();break;case IDC_OPEN:if(outputDir[0])ShellExecuteW(h,L"open",outputDir,NULL,NULL,SW_SHOWNORMAL);break;case IDC_TIPS:show_help();break;}return 0;
    case WM_CLOSE:if(running&&!automation&&MessageBoxW(h,L"停止当前任务并退出？已经导出的视频会保留。",L"任务仍在进行",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;if(running){if(jobObject)TerminateJobObject(jobObject,ERROR_CANCELLED);if(process)WaitForSingleObject(process,5000);}close_process();delete_temps();if(report!=INVALID_HANDLE_VALUE){CloseHandle(report);report=INVALID_HANDLE_VALUE;}DestroyWindow(h);return 0;
    case WM_DESTROY:KillTimer(h,1);PostQuitMessage(lastExit);return 0;
    }return DefWindowProcW(h,message,w,l);
}
static int self_test(const wchar_t *destination){
    const wchar_t *original[]={L"C:\\测试目录\\片段 O'Brien.mp4",L"C:\\with spaces\\",L"a\"quoted\"name",L"$(touch NOTHING)&x.mp4",L""};wchar_t cmd[4096]=L"";for(int i=0;i<5;++i)if(!append_argument(cmd,4096,original[i]))return 1;int count=0;wchar_t **parsed=CommandLineToArgvW(cmd,&count);int okay=parsed&&count==5;for(int i=0;i<count&&okay;++i)okay=!wcscmp(parsed[i],original[i]);if(parsed)LocalFree(parsed);
    char graph[8192];okay=okay&&filter_graph(graph,sizeof(graph),64,64,1,0)&&!filter_graph(graph,sizeof(graph),181,320,0,0);VideoInfo vi;okay=okay&&parse_video("Duration: 00:00:01.00\nStream #0:0: Video: h264, yuv420p, 320x180 [SAR 1:1 DAR 16:9]\n displaymatrix: rotation of -90.00 degrees\n",&vi)&&vi.width==320&&vi.height==180&&vi.rotated&&vi.duration==1;
    HANDLE h=CreateFileW(destination,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);if(h!=INVALID_HANDLE_VALUE){write_utf8(h,okay?L"PASS: Windows argument parsing, Unicode and special paths, video metadata and dimension validation.\r\n":L"FAIL\r\n");CloseHandle(h);}return okay?0:1;
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE previous,LPWSTR command,int show){
    (void)previous;(void)command;instance=inst;int argc=0;wchar_t **argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>=3&&!wcscmp(argv[1],L"--self-test")){int result=self_test(argv[2]);LocalFree(argv);return result;}
    if(argc>=7&&!wcscmp(argv[1],L"--batch-test")){automation=exitAfterBatch=1;copy_path(enginePath,argv[2]);copy_path(outputDir,argv[3]);targetW=_wtoi(argv[4]);targetH=_wtoi(argv[5]);mode=_wtoi(argv[6]);}
    HANDLE mutex=CreateMutexW(NULL,FALSE,L"Local\\LromVideoResizerWindows_v1");if(GetLastError()==ERROR_ALREADY_EXISTS&&!automation){HWND other=FindWindowW(L"LromVideoResizerWindow",NULL);if(other){ShowWindow(other,SW_RESTORE);SetForegroundWindow(other);}if(mutex)CloseHandle(mutex);LocalFree(argv);return 0;}
    SetProcessDPIAware();HDC dc=GetDC(NULL);int dpi=GetDeviceCaps(dc,LOGPIXELSY);ReleaseDC(NULL,dc);scaleFactor=dpi/96.0;font=CreateFontW(-MulDiv(10,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");headingFont=CreateFontW(-MulDiv(20,dpi,72),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&controls);
    wchar_t local[PATHCAP];if(SHGetFolderPathW(NULL,CSIDL_LOCAL_APPDATA,NULL,SHGFP_TYPE_CURRENT,local)!=S_OK)GetTempPathW(PATHCAP,local);join_path(cacheDir,local,L"LromVideoResizer");SHCreateDirectoryExW(NULL,cacheDir,NULL);if(!enginePath[0])join_path(enginePath,cacheDir,L"ffmpeg.exe");files=(Entry*)calloc(MAXFILES,sizeof(Entry));if(!files){LocalFree(argv);return 1;}
    WNDCLASSW klass={0};klass.lpfnWndProc=window_proc;klass.hInstance=inst;klass.hCursor=LoadCursorW(NULL,IDC_ARROW);klass.hIcon=LoadIconW(inst,MAKEINTRESOURCEW(101));klass.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);klass.lpszClassName=L"LromVideoResizerWindow";RegisterClassW(&klass);
    DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;RECT rect={0,0,px(920),px(701)};AdjustWindowRectEx(&rect,style,FALSE,WS_EX_APPWINDOW);window=CreateWindowExW(WS_EX_APPWINDOW,klass.lpszClassName,L"咸鱼尺寸助手 Windows v1.0.1 · Lrom",style,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,NULL,NULL,inst,NULL);
    if(!window){free(files);LocalFree(argv);return 1;}
    if(automation){for(int i=7;i<argc;++i)add_file(argv[i]);PostMessageW(window,WM_BEGIN_BATCH,0,0);}else{ShowWindow(window,show);UpdateWindow(window);}LocalFree(argv);
    MSG msg={0};while(GetMessageW(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}free(files);DeleteObject(font);DeleteObject(headingFont);if(mutex)CloseHandle(mutex);CoUninitialize();return (int)msg.wParam;
}

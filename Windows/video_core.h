#ifndef LROM_VIDEO_CORE_H
#define LROM_VIDEO_CORE_H
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct { int width,height,hdr,rotated; double duration; } VideoInfo;
static int dimensions_valid(int w,int h){return w>=64&&h>=64&&w<=4096&&h<=4096&&!(w%2)&&!(h%2);}
static int parse_video(const char *text,VideoInfo *out){
    memset(out,0,sizeof(*out));const char *d=strstr(text,"Duration:");int hh=0,mm=0;double ss=0;
    if(d&&sscanf(d,"Duration: %d:%d:%lf",&hh,&mm,&ss)==3)out->duration=hh*3600+mm*60+ss;
    const char *line=text;
    while((line=strstr(line,"Video: "))){
        const char *end=strchr(line,'\n');if(!end)end=line+strlen(line);
        char segment[4096];size_t n=(size_t)(end-line);if(n>=sizeof(segment))n=sizeof(segment)-1;memcpy(segment,line,n);segment[n]=0;
        if(!strstr(segment,"attached pic")){
            for(const char *p=segment;*p;++p){int w,h,used=0;
                if((*p>='0'&&*p<='9')&&(p==segment||p[-1]==' '||p[-1]==',')&&sscanf(p,"%dx%d%n",&w,&h,&used)==2&&w>0&&h>0&&used>0){
                    out->width=w;out->height=h;out->hdr=strstr(segment,"smpte2084")||strstr(segment,"arib-std-b67");
                    const char *r=strstr(text,"rotation of");double angle=0;if(r&&sscanf(r,"rotation of %lf",&angle)==1){int a=(int)(angle<0?-angle:angle);out->rotated=(a%180)==90;}
                    return 1;
                }
            }
        }line=end;
    }return 0;
}
static int filter_graph(char *out,size_t cap,int w,int h,int mode,int hdr){
    if(!dimensions_valid(w,h)||mode<0||mode>2)return 0;
    const char *normal="scale=w='max(2,trunc(ih*dar/2)*2)':h=ih,setsar=1";
    const char *tone=hdr?"zscale=t=linear:npl=100,format=gbrpf32le,zscale=p=bt709,tonemap=tonemap=hable:desat=0,zscale=t=bt709:m=bt709:r=tv,format=yuv420p,":"";
    const char *end=",setsar=1,fps=30,format=yuv420p[v]";int result;
    if(mode==0)result=snprintf(out,cap,"[0:v:0]%s%s,scale=%d:%d:force_original_aspect_ratio=decrease:force_divisible_by=2,pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=black%s",tone,normal,w,h,w,h,end);
    else if(mode==2)result=snprintf(out,cap,"[0:v:0]%s%s,scale=%d:%d:force_original_aspect_ratio=increase:force_divisible_by=2,crop=%d:%d%s",tone,normal,w,h,w,h,end);
    else{int bw=((w+7)/8)*2,bh=((h+7)/8)*2;if(bw<16)bw=16;if(bh<16)bh=16;int radius=(bw<bh?bw:bh)/4;if(radius>12)radius=12;if(radius<1)radius=1;
        result=snprintf(out,cap,"[0:v:0]%s%s,split=2[fg][bg];[bg]scale=%d:%d:force_original_aspect_ratio=increase:force_divisible_by=2,crop=%d:%d,boxblur=%d:2,scale=%d:%d[back];[fg]scale=%d:%d:force_original_aspect_ratio=decrease:force_divisible_by=2[front];[back][front]overlay=(W-w)/2:(H-h)/2:shortest=1%s",tone,normal,bw,bh,bw,bh,radius,w,h,w,h,end);
    }return result>=0&&(size_t)result<cap;
}
static double progress_percent(const char *text,double duration){
    const char *p=text;double value=0;while((p=strstr(p,"out_time_us="))){value=strtod(p+12,0)/1000000;p+=12;}if(duration<=0)return 0;double percent=value/duration*100;return percent<0?0:percent>99?99:percent;
}
// Microsoft command-line quoting. Arguments never pass through cmd.exe.
static int append_argument(wchar_t *cmd,size_t cap,const wchar_t *arg){
    size_t pos=wcslen(cmd);if(pos){if(pos+1>=cap)return 0;cmd[pos++]=L' ';}if(pos+1>=cap)return 0;cmd[pos++]=L'"';
    const wchar_t *p=arg;
    while(*p){size_t count=0;while(*p==L'\\'){++count;++p;}
        size_t emit=(*p==L'"'||!*p)?count*2:count;
        if(pos+emit+3>=cap)return 0;while(emit--)cmd[pos++]=L'\\';
        if(*p==L'"')cmd[pos++]=L'\\';if(*p)cmd[pos++]=*p++;
    }if(pos+2>cap)return 0;cmd[pos++]=L'"';cmd[pos]=0;return 1;
}
#endif

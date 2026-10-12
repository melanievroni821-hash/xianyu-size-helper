"""Real FFmpeg integration checks for the Windows C filter implementation.
The executable UI/process APIs require Windows; this test runs the filter logic on Linux.
"""
from pathlib import Path
import subprocess,json,tempfile,hashlib

ROOT=Path(__file__).resolve().parents[1]
CLI=ROOT/'build_win/filters_cli'
def ff(args):
    return subprocess.run(['ffmpeg','-v','error','-nostdin',*args],check=True,capture_output=True)
def info(p):return json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(p)]))
def pixel(p,x,y):return list(ff(['-i',str(p),'-vf',f'format=rgb24,crop=1:1:{x}:{y}','-frames:v','1','-f','rawvideo','-']).stdout[:3])
results=[]
with tempfile.TemporaryDirectory(prefix='lrom-win-filter-') as tmp:
    d=Path(tmp);source=d/"中文 O'Brien & $(touch SHOULD_NOT_EXIST).mp4"
    ff(['-f','lavfi','-i','color=c=0x28c850:s=320x180:r=24:d=1','-f','lavfi','-i','sine=frequency=440:duration=1','-c:v','libx264','-c:a','aac','-shortest',str(source)])
    def convert(src,w,h,mode,hdr,label,audio):
        graph=subprocess.check_output([str(CLI),str(w),str(h),str(mode),str(hdr)],text=True).strip()
        out=d/(label+'.mp4')
        args=['-n','-i',str(src),'-filter_complex',graph,'-map','[v]','-map','0:a:0?','-c:v','libx264','-preset','fast','-crf','20','-pix_fmt','yuv420p','-threads','4','-filter_complex_threads','2','-c:a','aac','-b:a','192k','-ar','48000','-movflags','+faststart','-map_metadata','-1','-metadata:s:v:0','rotate=0']
        if hdr:args+=['-color_primaries','bt709','-color_trc','bt709','-colorspace','bt709']
        ff(args+['-f','mp4',str(out)]);data=info(out);v=next(s for s in data['streams'] if s['codec_type']=='video')
        assert (v['width'],v['height'])==(w,h)
        assert v['codec_name']=='h264' and v['pix_fmt']=='yuv420p' and v['r_frame_rate']=='30/1' and v['sample_aspect_ratio']=='1:1'
        assert any(s['codec_type']=='audio' for s in data['streams'])==audio
        assert abs(float(data['format']['duration'])-1)<0.12
        results.append(label);return out
    p=convert(source,180,320,0,0,'black_padding_audio',True)
    assert max(pixel(p,3,3))<8 and pixel(p,90,160)[1]>140
    p=convert(source,180,320,1,0,'blur_background_audio',True);assert pixel(p,3,3)[1]>140
    p=convert(source,180,320,2,0,'center_crop_audio',True);assert pixel(p,3,3)[1]>140
    convert(source,1080,1920,0,0,'default_1080x1920',True)
    convert(source,1080,1080,0,0,'square',True)
    convert(source,1080,1350,2,0,'portrait_4x5',True)
    silent=d/'silent.mp4';ff(['-f','lavfi','-i','testsrc2=s=180x320:r=25:d=1','-c:v','libx264',str(silent)])
    convert(silent,320,180,1,0,'silent_landscape',False)
    rotated=d/'rotated.mov';ff(['-display_rotation','90','-i',str(source),'-c','copy',str(rotated)])
    p=convert(rotated,180,320,0,0,'rotation_metadata',True);assert pixel(p,10,10)[1]>140
    sar=d/'SAR.mp4';ff(['-f','lavfi','-i','color=c=green:s=180x180:r=30:d=1','-vf','setsar=2','-c:v','libx264',str(sar)])
    p=convert(sar,180,320,0,0,'anamorphic_pixels',False);assert max(pixel(p,90,90))<8
    hdr=d/'hdr.mp4';ff(['-f','lavfi','-i','testsrc2=s=320x180:r=30:d=1','-c:v','libx264','-color_primaries','bt2020','-color_trc','smpte2084','-colorspace','bt2020nc',str(hdr)])
    p=convert(hdr,180,320,0,1,'HDR_to_SDR',False);assert next(s for s in info(p)['streams'] if s['codec_type']=='video')['color_transfer']=='bt709'
    convert(source,64,64,1,0,'small_dimensions_blur',True)
    assert not (ROOT/'SHOULD_NOT_EXIST').exists()
    results.append('unicode_and_shell_metacharacters')
result={'passed':len(results),'cases':results,'platform':'Linux FFmpeg; native Windows runtime not executed'}
(ROOT/'build_win/filter_integration.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))

/* Shared processing logic: runs in macOS JXA and Node.js. */
var ResizeCore = (function () {
    'use strict';
    var extensions = /\.(mp4|mov|m4v|mkv|avi|webm|mts|m2ts|ts)$/i;
    function parseInfo(text) {
        var duration = /Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?)/.exec(text);
        var video = text.split('\n').filter(function (line) { return /Stream .*Video:/.test(line) && !/attached pic/.test(line); })[0];
        if (!video) throw new Error('未找到可处理的视频轨道，文件可能损坏或只有音频。');
        var size = /(?:^|[,\s])(\d{2,6})x(\d{2,6})(?:[,\s\[]|$)/.exec(video);
        if (!size) throw new Error('无法识别视频分辨率。');
        var rotation = /rotation of\s*([-\d.]+)\s*degrees/i.exec(text);
        var rotated = rotation && Math.abs(Math.round(Number(rotation[1]))) % 180 === 90;
        var hdr = /smpte2084|arib-std-b67/i.test(video);
        return {
            width: Number(size[1]), height: Number(size[2]), rotated: !!rotated, hdr: hdr,
            duration: duration ? Number(duration[1])*3600+Number(duration[2])*60+Number(duration[3]) : 0
        };
    }
    function dimensions(width, height) {
        width = Number(width); height = Number(height);
        if (!Number.isInteger(width) || !Number.isInteger(height) || width < 64 || height < 64 || width > 4096 || height > 4096 || width % 2 || height % 2)
            throw new Error('宽高须为 64–4096 之间的偶数。');
        return [width, height];
    }
    function graph(width, height, mode, hdr) {
        dimensions(width, height);
        if (['pad','blur','crop'].indexOf(mode) < 0) throw new Error('未知的画面处理方式。');
        var w = width, h = height;
        // Square pixels before fitting, including anamorphic footage. FFmpeg auto-rotates input first.
        var normalize = "scale=w='max(2,trunc(ih*dar/2)*2)':h=ih,setsar=1";
        if (hdr) normalize = 'zscale=t=linear:npl=100,format=gbrpf32le,zscale=p=bt709,tonemap=tonemap=hable:desat=0,zscale=t=bt709:m=bt709:r=tv,format=yuv420p,' + normalize;
        var fit = 'scale='+w+':'+h+':force_original_aspect_ratio=decrease:force_divisible_by=2';
        var fill = 'scale='+w+':'+h+':force_original_aspect_ratio=increase:force_divisible_by=2,crop='+w+':'+h;
        var end = ',setsar=1,fps=30,format=yuv420p[v]';
        if (mode === 'pad') return '[0:v:0]'+normalize+','+fit+',pad='+w+':'+h+':(ow-iw)/2:(oh-ih)/2:color=black'+end;
        if (mode === 'crop') return '[0:v:0]'+normalize+','+fill+end;
        var bw = Math.max(16, Math.ceil(w/8)*2), bh = Math.max(16, Math.ceil(h/8)*2);
        var radius = Math.max(1, Math.min(12, Math.floor(Math.min(bw,bh)/4)));
        return '[0:v:0]'+normalize+',split=2[fg][bg];[bg]scale='+bw+':'+bh+':force_original_aspect_ratio=increase:force_divisible_by=2,crop='+bw+':'+bh+',boxblur='+radius+':2,scale='+w+':'+h+'[back];[fg]'+fit+'[front];[back][front]overlay=(W-w)/2:(H-h)/2:shortest=1'+end;
    }
    function argumentsFor(input, output, options) {
        var args = ['-hide_banner','-nostdin','-n','-i',input,'-filter_complex',graph(options.width,options.height,options.mode,options.hdr),'-map','[v]','-map','0:a:0?','-c:v','libx264','-preset','fast','-crf',String(options.crf || 20),'-pix_fmt','yuv420p','-threads','4','-filter_complex_threads','2','-c:a','aac','-b:a','192k','-ar','48000','-movflags','+faststart','-map_metadata','-1','-metadata:s:v:0','rotate=0'];
        if (options.hdr) args = args.concat(['-color_primaries','bt709','-color_trc','bt709','-colorspace','bt709']);
        return args.concat(['-progress','pipe:1','-nostats','-f','mp4',output]);
    }
    function percent(progress, duration) {
        var regex = /out_time_us=(\d+)/g, match, value = 0;
        while ((match = regex.exec(progress))) value = Number(match[1])/1000000;
        return duration > 0 ? Math.min(99, Math.max(0, value/duration*100)) : 0;
    }
    function destination(input, dir, width, height, mode, exists) {
        var basename = input.split('/').pop().replace(/\.[^.]+$/, '');
        var label = {pad:'完整画面',blur:'模糊补边',crop:'居中裁切'}[mode];
        var stem = dir+'/'+basename+'_'+width+'x'+height+'_'+label;
        var path = stem+'.mp4', index = 2;
        while (exists(path)) path = stem+'_'+(index++)+'.mp4';
        return path;
    }
    return {extensions:extensions,parseInfo:parseInfo,dimensions:dimensions,graph:graph,argumentsFor:argumentsFor,percent:percent,destination:destination};
}());
if (typeof module !== 'undefined') module.exports = ResizeCore;

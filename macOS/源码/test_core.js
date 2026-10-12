'use strict';
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFileSync } = require('child_process');
const nearby=path.join(__dirname,'..');
const base = fs.existsSync(path.join(nearby,'咸鱼尺寸助手.app')) ? nearby : path.join(nearby, 'deliverable', 'Lrom_Video_Resizer_Mac');
const core = require(path.join(base, '咸鱼尺寸助手.app', 'Contents', 'Resources', 'core.js'));
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'lrom-video-test-'));
const results = [];
function ff(args) { return execFileSync('ffmpeg', ['-hide_banner', '-loglevel', 'error', ...args], {encoding:'utf8', maxBuffer:20*1024*1024}); }
function info(file) { return JSON.parse(execFileSync('ffprobe',['-v','error','-show_streams','-show_format','-of','json',file],{encoding:'utf8'})); }
function probe(file) { let text='';try {execFileSync('ffmpeg',['-hide_banner','-nostdin','-i',file],{encoding:'utf8',stdio:['ignore','pipe','pipe']});}catch(e){text=e.stderr.toString();}return core.parseInfo(text); }
function check(output,w,h,audio) {
    const data=info(output),v=data.streams.find(x=>x.codec_type==='video');
    assert.equal(v.width,w);assert.equal(v.height,h);assert.equal(v.codec_name,'h264');assert.equal(v.pix_fmt,'yuv420p');assert.equal(v.sample_aspect_ratio,'1:1');assert.equal(v.r_frame_rate,'30/1');
    assert.equal(Boolean(data.streams.find(x=>x.codec_type==='audio')),audio);
    if(audio) {const a=data.streams.find(x=>x.codec_type==='audio');assert.equal(a.codec_name,'aac');assert.equal(a.sample_rate,'48000');}
    assert(Math.abs(Number(data.format.duration)-1)<0.12);
}
function convert(input,mode,w,h,label,audio) {
    const output=path.join(dir,label+'.mp4'),meta=probe(input);
    execFileSync('ffmpeg',core.argumentsFor(input,output,{width:w,height:h,mode,hdr:meta.hdr}),{stdio:['ignore','pipe','pipe'],maxBuffer:20*1024*1024});
    check(output,w,h,audio);results.push(label);return output;
}
function pixel(file,x,y) {
    return [...execFileSync('ffmpeg',['-v','error','-i',file,'-vf',`format=rgb24,crop=1:1:${x}:${y}`,'-frames:v','1','-f','rawvideo','-'])].slice(0,3);
}
try {
    const source=path.join(dir,"中文 素材 O'Brien $(touch SHOULD_NOT_EXIST).mp4");
    ff(['-f','lavfi','-i','color=c=0x28c850:s=320x180:r=24:d=1','-f','lavfi','-i','sine=frequency=440:duration=1','-c:v','libx264','-c:a','aac','-shortest',source]);
    const pad=convert(source,'pad',180,320,'01_black_with_audio',true);
    assert(pixel(pad,3,3).every(x=>x<8));assert(pixel(pad,90,160)[1]>140);
    const blur=convert(source,'blur',180,320,'02_blur_with_audio',true);
    assert(pixel(blur,3,3)[1]>140);
    const crop=convert(source,'crop',180,320,'03_crop_with_audio',true);
    assert(pixel(crop,3,3)[1]>140);
    convert(source,'pad',1080,1920,'04_default_1080x1920',true);
    const silent=path.join(dir,'silent.mp4');
    ff(['-f','lavfi','-i','testsrc2=s=180x320:r=25:d=1','-c:v','libx264',silent]);
    convert(silent,'blur',320,180,'05_silent_landscape',false);
    const rotated=path.join(dir,'rotated.mov');
    ff(['-display_rotation','90','-i',source,'-c','copy',rotated]);
    assert(probe(rotated).rotated);const rp=convert(rotated,'pad',180,320,'06_rotation',true);
    assert(pixel(rp,10,10)[1]>140);assert(!JSON.stringify(info(rp)).includes('"rotation": -90'));
    const anamorphic=path.join(dir,'anamorphic.mp4');
    ff(['-f','lavfi','-i','color=c=green:s=180x180:r=30:d=1','-vf','setsar=2','-c:v','libx264',anamorphic]);
    const ap=convert(anamorphic,'pad',180,320,'07_anamorphic_SAR',false);
    assert(pixel(ap,90,90).every(x=>x<8));assert(pixel(ap,90,160)[1]>90);
    const hdr=path.join(dir,'hdr.mp4');
    ff(['-f','lavfi','-i','testsrc2=s=320x180:r=30:d=1','-c:v','libx264','-color_primaries','bt2020','-color_trc','smpte2084','-colorspace','bt2020nc',hdr]);
    assert(probe(hdr).hdr);const hp=convert(hdr,'pad',180,320,'08_HDR_to_SDR',false);
    const hv=info(hp).streams[0];assert.equal(hv.color_transfer,'bt709');assert.equal(hv.color_primaries,'bt709');
    convert(source,'pad',1080,1080,'09_square',true);
    convert(source,'crop',1080,1350,'10_portrait_4x5',true);
    const before=fs.readFileSync(pad);
    // Some FFmpeg builds return zero on -n refusal. The essential guarantee is unchanged bytes.
    try{execFileSync('ffmpeg',core.argumentsFor(source,pad,{width:180,height:320,mode:'pad'}),{stdio:['ignore','pipe','pipe']});}catch(_){}
    assert.deepEqual(fs.readFileSync(pad),before);results.push('11_no_overwrite');
    const first=core.destination(source,dir,180,320,'pad',()=>false);
    const second=core.destination(source,dir,180,320,'pad',p=>p===first);
    assert.notEqual(first,second);assert(second.endsWith('_2.mp4'));results.push('12_duplicate_filename');
    assert.throws(()=>core.dimensions(181,320));assert.throws(()=>core.dimensions(0,320));assert.throws(()=>core.parseInfo('invalid data'));assert.throws(()=>core.graph(180,320,'bad'));
    assert.equal(core.percent('out_time_us=250000\nout_time_us=500000\n',1),50);results.push('13_invalid_inputs_and_progress');
    assert(!fs.existsSync(path.join(process.cwd(),'SHOULD_NOT_EXIST')));results.push('14_unicode_and_shell_metacharacters');
    // Save frames for visual inspection during development only.
    fs.mkdirSync(path.join(process.cwd(),'tests'),{recursive:true});
    for(const [name,file] of [['pad',pad],['blur',blur],['crop',crop]]) ff(['-i',file,'-frames:v','1',path.join(process.cwd(),'tests',name+'.png')]);
    fs.writeFileSync(path.join(process.cwd(),'tests','test_results.json'),JSON.stringify({passed:results.length,cases:results,ffmpeg:execFileSync('ffmpeg',['-version'],{encoding:'utf8'}).split('\n')[0]},null,2));
    console.log(JSON.stringify({passed:results.length,cases:results},null,2));
} finally {fs.rmSync(dir,{recursive:true,force:true});}

ObjC.import('Cocoa');
ObjC.import('Foundation');

// JavaScript null bridges to NSNull, not an Objective-C nil pointer.
var cocoaNil = $();

var fm = $.NSFileManager.defaultManager;
var app = $.NSApplication.sharedApplication;
var ResizeCore, root, ffmpeg, engineDir, engineArch, engineJob=null, controller, win, queueBox, statusLabel, progressBar, outputLabel;
var ratioSelect, modeSelect, qualitySelect, widthBox, heightBox, startButton, cancelButton;
var buttons = [], files = [], events = [], outDir = '', timer, running = false, job = null, cursor = 0, cancelled = false, successes = 0, failures = 0, runOptions;
var presets = [[1080,1920],[720,1280],[1920,1080],[1080,1080],[1080,1350]];
var modeKeys = ['pad','blur','crop'];

function str(value) { return ObjC.unwrap(value); }
function read(path) { var value = $.NSString.stringWithContentsOfFileEncodingError($(path), $.NSUTF8StringEncoding, Ref()); return (value ? str(value) : '') || ''; }
function write(path, text) { return $(String(text)).writeToFileAtomicallyEncodingError($(path),true,$.NSUTF8StringEncoding,Ref()); }
function exists(path) { return Boolean(fm.fileExistsAtPath($(path))); }
function remove(path) { if (path && exists(path)) fm.removeItemAtPathError($(path),Ref()); }
function createEmptyFile(path) { if(!fm.createFileAtPathContentsAttributes($(path),$.NSData.data,$.NSDictionary.dictionary)) throw new Error('无法创建临时文件：'+path); }
function base(path) { return path.split('/').pop(); }
function log(text) { events.push(text); refresh(); }
function refresh() {
    var list = files.map(function (f,i) { return (i+1)+'. '+base(f); }).join('\n');
    queueBox.string = $('待处理视频：'+files.length+' 个\n'+(list || '将视频或素材文件夹拖到窗口，或者点击“添加视频”。')+'\n\n'+events.slice(-100).join('\n'));
    queueBox.scrollRangeToVisible($.NSMakeRange(Math.max(0,str(queueBox.string).length-1),0));
}
function alert(title, detail) { var a=$.NSAlert.alloc.init; a.messageText=$(title); a.informativeText=$(detail); a.addButtonWithTitle($('知道了')); a.runModal; }
function promptStop() {
    if (!running) return true;
    var a=$.NSAlert.alloc.init; a.messageText=$('正在批量处理视频'); a.informativeText=$('停止会删除当前未完成文件，已经导出的视频会保留。'); a.addButtonWithTitle($('继续处理')); a.addButtonWithTitle($('停止并退出'));
    if (Number(a.runModal) === 1000) return false;
    if (job && job.task.running) { job.task.terminate; job.task.waitUntilExit; }
    if (engineJob && engineJob.task.running) { engineJob.task.terminate; engineJob.task.waitUntilExit; }
    if (engineJob) {try{engineJob.handle.closeFile;}catch(_){}remove(engineJob.path);engineJob=null;}
    if (job) cleanup(job);
    running=false; job=null;
    return true;
}
function probe(path) {
    var logPath=str($.NSTemporaryDirectory())+'lrom-probe-'+str($.NSUUID.UUID.UUIDString)+'.txt';
    createEmptyFile(logPath);
    var handle=$.NSFileHandle.fileHandleForWritingAtPath($(logPath));
    var task=$.NSTask.alloc.init; task.launchPath=$(ffmpeg); task.arguments=$(['-hide_banner','-nostdin','-i',path]); task.standardOutput=handle; task.standardError=handle; task.standardInput=$.NSFileHandle.fileHandleWithNullDevice;
    try { task.launch; task.waitUntilExit; handle.closeFile; return ResizeCore.parseInfo(read(logPath)); }
    finally { try { handle.closeFile; } catch (_) {} remove(logPath); }
}
function addPaths(paths) {
    if (running) return;
    var skipped=0;
    function addFile(path) {
        if (ResizeCore.extensions.test(path) && exists(path)) { if (files.indexOf(path)<0) files.push(path); }
        else skipped++;
    }
    paths.forEach(function(path) {
        // A folder contributes its immediate video children. Do not traverse output or unrelated subfolders.
        var isDir=Ref();
        if (fm.fileExistsAtPathIsDirectory($(path),isDir) && isDir[0]) {
            var children=ObjC.deepUnwrap(fm.contentsOfDirectoryAtPathError($(path),Ref())) || [];
            children.sort().forEach(function(name) { var child=path+'/'+name, childDir=Ref(); if (name.charAt(0)!=='.' && fm.fileExistsAtPathIsDirectory($(child),childDir) && !childDir[0]) addFile(child); });
        } else addFile(path);
    });
    if (!outDir && files.length) setOutput(files[0].slice(0,files[0].lastIndexOf('/'))+'/尺寸转换结果');
    if (skipped) log('忽略 '+skipped+' 个非视频文件。');
    refresh(); statusLabel.stringValue=$('已添加 '+files.length+' 个视频');
}
function chooseFiles(folder) {
    if (running) return;
    var p=$.NSOpenPanel.openPanel; p.canChooseFiles=!folder; p.canChooseDirectories=folder; p.allowsMultipleSelection=true; p.prompt=$('添加');
    if (Number(p.runModal)===1) { var paths=[]; for(var i=0;i<Number(p.URLs.count);i++) paths.push(str(p.URLs.objectAtIndex(i).path)); addPaths(paths); }
}
function setOutput(path) { outDir=path; outputLabel.stringValue=$('导出目录：'+path); outputLabel.toolTip=$(path); }
function selectOutput() { if (running) return; var p=$.NSOpenPanel.openPanel; p.canChooseFiles=false; p.canChooseDirectories=true; p.canCreateDirectories=true; p.prompt=$('选择导出目录'); if(Number(p.runModal)===1) setOutput(str(p.URL.path)); }
function setBusy(value) { buttons.forEach(function(b){b.enabled=!value;}); startButton.enabled=!value; cancelButton.enabled=value; ratioSelect.enabled=!value; modeSelect.enabled=!value; qualitySelect.enabled=!value; widthBox.enabled=!value; heightBox.enabled=!value; }
function cleanup(j) { if(!j) return; try{j.progressHandle.closeFile;}catch(_){} try{j.logHandle.closeFile;}catch(_){} remove(j.progressPath); remove(j.logPath); remove(j.partial); }
function start() {
    if (running) return;
    if (!files.length) { alert('先添加视频','可以多选视频，或把文件夹拖进窗口。'); return; }
    try {
        var dims=ResizeCore.dimensions(str(widthBox.stringValue),str(heightBox.stringValue));
        if(!outDir) throw new Error('请选择导出目录。');
        if(!fm.createDirectoryAtPathWithIntermediateDirectoriesAttributesError($(outDir),true,$.NSDictionary.dictionary,Ref()) || !fm.isWritableFileAtPath($(outDir))) throw new Error('导出目录不可写，请选择有写入权限的文件夹。');
        runOptions={width:dims[0],height:dims[1],mode:modeKeys[Number(modeSelect.indexOfSelectedItem)],crf:[20,18,23][Number(qualitySelect.indexOfSelectedItem)]};
        running=true; cancelled=false; cursor=0; successes=0; failures=0; events=[]; setBusy(true); progressBar.doubleValue=0;
        log('开始：'+dims.join('×')+' · '+str(modeSelect.titleOfSelectedItem)+' · MP4 / H.264 / 30fps');
        if(!exists(ffmpeg)) installEngine();
    } catch(e) { running=false;setBusy(false);if(engineJob){try{if(engineJob.task.running){engineJob.task.terminate;engineJob.task.waitUntilExit;}engineJob.handle.closeFile;}catch(_){}remove(engineJob.path);engineJob=null;}progressBar.stopAnimation(cocoaNil);progressBar.indeterminate=false;alert('无法开始',String(e.message || e)); }
}
function installEngine() {
    var path=str($.NSTemporaryDirectory())+'lrom-install-'+str($.NSUUID.UUID.UUIDString)+'.log';
    createEmptyFile(path);
    var handle=$.NSFileHandle.fileHandleForWritingAtPath($(path)), task=$.NSTask.alloc.init;
    task.launchPath=$('/bin/bash');task.arguments=$([root+'/install_engine.sh',engineDir,engineArch]);task.standardOutput=handle;task.standardError=handle;task.standardInput=$.NSFileHandle.fileHandleWithNullDevice;
    engineJob={path:path,handle:handle,task:task};task.launch;progressBar.indeterminate=true;progressBar.startAnimation(cocoaNil);
    log('首次使用：正在从 PyPI 下载并校验转码引擎（约 21–25MB），完成后自动开始处理。');
    statusLabel.stringValue=$('首次准备转码引擎，请保持联网…');
}
function pollEngine() {
    var e=engineJob;
    if(e.task.running){statusLabel.stringValue=$('首次准备：'+read(e.path).trim().split('\n').slice(-1)[0]);return;}
    var code=Number(e.task.terminationStatus),details=read(e.path);e.handle.closeFile;remove(e.path);engineJob=null;progressBar.stopAnimation(cocoaNil);progressBar.indeterminate=false;
    if(cancelled){finish();return;}
    if(code!==0 || !exists(ffmpeg)) {running=false;setBusy(false);log('引擎准备失败：'+details.trim());statusLabel.stringValue=$('转码引擎准备失败，请检查网络后重试');return;}
    log('转码引擎准备完成，以后这台电脑可以离线处理。');
}
function startNext() {
    if(cancelled || cursor>=files.length) { finish(); return; }
    var input=files[cursor], j=null;
    try {
        var info=probe(input), options=Object.assign({},runOptions,{hdr:info.hdr});
        var destination=ResizeCore.destination(input,outDir,options.width,options.height,options.mode,exists);
        var token=str($.NSUUID.UUID.UUIDString);
        j={input:input,info:info,output:destination,partial:outDir+'/.lrom-'+token+'.partial.mp4',progressPath:str($.NSTemporaryDirectory())+'lrom-'+token+'.progress',logPath:str($.NSTemporaryDirectory())+'lrom-'+token+'.log'};
        createEmptyFile(j.progressPath); createEmptyFile(j.logPath);
        j.progressHandle=$.NSFileHandle.fileHandleForWritingAtPath($(j.progressPath)); j.logHandle=$.NSFileHandle.fileHandleForWritingAtPath($(j.logPath));
        j.task=$.NSTask.alloc.init; j.task.launchPath=$(ffmpeg); j.task.arguments=$(ResizeCore.argumentsFor(input,j.partial,options)); j.task.standardOutput=j.progressHandle; j.task.standardError=j.logHandle; j.task.standardInput=$.NSFileHandle.fileHandleWithNullDevice;
        job=j; j.task.launch;
        log('['+(cursor+1)+'/'+files.length+'] '+base(input)+' · '+info.width+'×'+info.height+(info.rotated?'（含旋转标记）':'')+(info.hdr?' · HDR 转 SDR':''));
    } catch(e) { log('失败：'+base(input)+' — '+String(e.message || e)); failures++; cursor++; cleanup(j); job=null; }
}
function poll() {
    if(!running) return;
    if(engineJob){pollEngine();return;}
    if(!job) { startNext(); return; }
    var j=job;
    if(j.task.running) {
        var p=ResizeCore.percent(read(j.progressPath),j.info.duration);
        progressBar.doubleValue=(cursor+p/100)/files.length*100;
        statusLabel.stringValue=$('正在处理 '+(cursor+1)+'/'+files.length+' · '+Math.floor(p)+'% · '+base(j.input));
        return;
    }
    var code=Number(j.task.terminationStatus), failure=read(j.logPath);
    j.progressHandle.closeFile; j.logHandle.closeFile;
    if(cancelled) log('已停止：'+base(j.input));
    else if(code===0 && exists(j.partial)) {
        try {
            var verified=probe(j.partial);
            if(verified.width!==runOptions.width || verified.height!==runOptions.height || verified.duration<=0)throw new Error('导出校验失败：分辨率或时长异常，未保存为完成结果。');
            // Reserve no filenames in advance: recheck immediately before moving the completed output.
            j.output=ResizeCore.destination(j.input,outDir,runOptions.width,runOptions.height,runOptions.mode,exists);
            if(!fm.moveItemAtPathToPathError($(j.partial),$(j.output),Ref())) throw new Error('无法保存完成文件，可能空间不足或目录权限发生变化。');
            successes++; log('完成：'+base(j.output));
        } catch(e) {failures++;log('保存失败：'+base(j.input)+' — '+String(e.message || e));}
    } else { failures++; log('失败：'+base(j.input)+' — '+failure.trim().split('\n').slice(-6).join(' / ')); }
    cleanup(j); job=null; cursor++; progressBar.doubleValue=cursor/files.length*100;
}
function finish() {
    running=false;setBusy(false);if(!cancelled)progressBar.doubleValue=100;
    var pending=Math.max(0,files.length-cursor);
    var message=(cancelled?'已停止':'批量完成')+'：成功 '+successes+'，失败 '+failures+(cancelled?'，剩余 '+pending+' 个未处理':'');
    statusLabel.stringValue=$(message);log(message);
    var report=outDir+'/处理报告_'+new Date().toISOString().replace(/[:.]/g,'-')+'.txt';
    if(!write(report,'咸鱼尺寸助手 v1.1.1\n'+new Date().toLocaleString()+'\n'+message+'\n\n'+events.join('\n')+'\n')) log('处理报告保存失败，视频结果仍可在导出目录查看。');
}
function action(sender) {
    try {
        var tag=Number(sender.tag);
        if(tag===1)chooseFiles(false);
        if(tag===2)chooseFiles(true);
        if(tag===3 && !running){files=[];events=[];refresh();statusLabel.stringValue=$('已清空列表');}
        if(tag===4)selectOutput();
        if(tag===5)start();
        if(tag===6 && running){cancelled=true;cancelButton.enabled=false;if(job && job.task.running)job.task.terminate;if(engineJob && engineJob.task.running)engineJob.task.terminate;statusLabel.stringValue=$('正在停止…');}
        if(tag===7 && outDir)$.NSWorkspace.sharedWorkspace.openURL($.NSURL.fileURLWithPath($(outDir)));
        if(tag===8){var d=presets[Number(ratioSelect.indexOfSelectedItem)];widthBox.stringValue=$(String(d[0]));heightBox.stringValue=$(String(d[1]));}
        if(tag===9)alert('使用提示','默认：1080×1920，保留画面补黑边。\n\n模糊补边：主体完整保留，空白区域用模糊画面填满。\n居中裁切：会裁掉两侧或上下内容，请先试一条检查字幕和产品。\n\n原文件不会被覆盖。输出统一为 MP4 / H.264 / AAC / 30fps；无音轨的视频保持无声。HDR 素材会转为 SDR。\n\n本工具修复尺寸和编码，不保证通过平台其他审核；低清视频放大也不会恢复细节。');
    }catch(e){alert('操作出错',String(e.message || e));}
}
ObjC.registerSubclass({name:'LromController',superclass:'NSObject',methods:{
    'clicked:':{types:['void',['id']],implementation:function(sender){action(sender);}},
    'poll:':{types:['void',['id']],implementation:function(){try{poll();}catch(e){if(job && job.task.running){job.task.terminate;job.task.waitUntilExit;}cleanup(job);job=null;if(engineJob){if(engineJob.task.running){engineJob.task.terminate;engineJob.task.waitUntilExit;}try{engineJob.handle.closeFile;}catch(_){}remove(engineJob.path);engineJob=null;}progressBar.stopAnimation(cocoaNil);progressBar.indeterminate=false;running=false;setBusy(false);log('处理异常：'+String(e.message || e));statusLabel.stringValue=$('处理异常，请查看记录后重试');}}},
    'windowShouldClose:':{types:['bool',['id']],implementation:function(){if(!promptStop())return false;app.terminate(cocoaNil);return true;}},
    'applicationShouldTerminate:':{types:['unsigned long',['id']],implementation:function(){return promptStop()?1:0;}}
}});
ObjC.registerSubclass({name:'LromDropView',superclass:'NSView',methods:{
    'draggingEntered:':{types:['unsigned long',['id']],implementation:function(){return running?0:1;}},
    'performDragOperation:':{types:['bool',['id']],implementation:function(sender){try{var p=ObjC.deepUnwrap(sender.draggingPasteboard.propertyListForType($.NSFilenamesPboardType));if(p){addPaths(p);return true;}}catch(e){log('拖入失败，请使用添加视频按钮。');}return false;}}
}});
function label(view,text,x,y,w,h,size,bold) {var t=$.NSTextField.alloc.initWithFrame($.NSMakeRect(x,y,w,h));t.stringValue=$(text);t.bezeled=false;t.drawsBackground=false;t.editable=false;t.selectable=true;t.font=bold?$.NSFont.boldSystemFontOfSize(size||13):$.NSFont.systemFontOfSize(size||13);view.addSubview(t);return t;}
function button(view,title,x,y,w,tag){var b=$.NSButton.alloc.initWithFrame($.NSMakeRect(x,y,w,32));b.title=$(title);b.bezelStyle=$.NSBezelStyleRounded;b.target=controller;b.action='clicked:';b.tag=tag;view.addSubview(b);return b;}
function popup(view,titles,x,y,w,tag){var p=$.NSPopUpButton.alloc.initWithFramePullsDown($.NSMakeRect(x,y,w,28),false);p.addItemsWithTitles($(titles));p.target=controller;p.action='clicked:';p.tag=tag;view.addSubview(p);return p;}
function field(view,value,x,y,w){var t=$.NSTextField.alloc.initWithFrame($.NSMakeRect(x,y,w,26));t.stringValue=$(value);view.addSubview(t);return t;}

function run(argv) {
    root=argv[0];
    if(!root)throw new Error('请从软件包内启动。');
    ResizeCore=(new Function(read(root+'/core.js')+'; return ResizeCore;'))();
    var arch;
    // uname is invoked with argument arrays, never a shell command containing user paths.
    var p=$.NSPipe.pipe,t=$.NSTask.alloc.init;t.launchPath=$('/usr/bin/uname');t.arguments=$(['-m']);t.standardOutput=p;t.launch;t.waitUntilExit;
    arch=str($.NSString.alloc.initWithDataEncoding(p.fileHandleForReading.readDataToEndOfFile,$.NSUTF8StringEncoding)).trim();
    engineArch=arch==='arm64'?'arm64':'x86_64';
    engineDir=str($.NSHomeDirectory())+'/Library/Application Support/LromVideoResizer';
    ffmpeg=engineDir+'/ffmpeg-'+engineArch;
    app.setActivationPolicy($.NSApplicationActivationPolicyRegular);
    controller=$.LromController.alloc.init;app.delegate=controller;
    var menu=$.NSMenu.alloc.init, item=$.NSMenuItem.alloc.init, submenu=$.NSMenu.alloc.init;
    menu.addItem(item);item.submenu=submenu;var quit=$.NSMenuItem.alloc.initWithTitleActionKeyEquivalent($('退出尺寸助手'),'terminate:',$('q'));submenu.addItem(quit);app.mainMenu=menu;
    win=$.NSWindow.alloc.initWithContentRectStyleMaskBackingDefer($.NSMakeRect(0,0,880,670),$.NSWindowStyleMaskTitled|$.NSWindowStyleMaskClosable|$.NSWindowStyleMaskMiniaturizable,$.NSBackingStoreBuffered,false);
    win.title=$('咸鱼尺寸助手 v1.1.1 · Lrom');win.delegate=controller;win.releasedWhenClosed=false;
    var view=$.LromDropView.alloc.initWithFrame($.NSMakeRect(0,0,880,670));view.registerForDraggedTypes($([str($.NSFilenamesPboardType)]));win.contentView=view;
    label(view,'批量改尺寸，一次导出',26,607,660,36,26,true);
    label(view,'本地处理 · 原视频保留 · 首次自动准备引擎，随后离线使用',28,582,700,24,13,false);
    button(view,'使用提示',755,610,96,9);
    buttons=[button(view,'添加视频',24,530,116,1),button(view,'添加文件夹',148,530,126,2),button(view,'清空列表',282,530,106,3)];
    label(view,'目标尺寸',28,490,90,24,13,true);
    ratioSelect=popup(view,['竖屏 9:16 · 1080×1920','竖屏 9:16 · 720×1280','横屏 16:9 · 1920×1080','方形 1:1 · 1080×1080','竖图 4:5 · 1080×1350'],112,488,245,8);
    label(view,'宽',390,490,25,22);widthBox=field(view,'1080',417,489,72);label(view,'× 高',500,490,43,22);heightBox=field(view,'1920',546,489,72);
    label(view,'画面处理',28,447,90,24,13,true);modeSelect=popup(view,['保留完整画面 · 黑边','保留完整画面 · 模糊背景','居中裁切 · 铺满（会裁内容）'],112,446,280,0);
    label(view,'画质',418,447,44,24,13,true);qualitySelect=popup(view,['标准清晰','更清晰（文件更大）','较小文件'],466,446,215,0);
    label(view,'建议先试 1 条；原片字幕贴边时，选择保留画面。自定义宽高须为偶数。',28,412,800,24,12,false);
    outputLabel=label(view,'导出目录：默认在第一条原视频旁新建“尺寸转换结果”',28,370,660,30,12,false);
    buttons.push(button(view,'选择导出目录',714,374,138,4));
    var scroll=$.NSScrollView.alloc.initWithFrame($.NSMakeRect(28,156,824,206));scroll.hasVerticalScroller=true;scroll.borderType=$.NSBezelBorder;
    queueBox=$.NSTextView.alloc.initWithFrame($.NSMakeRect(0,0,800,206));queueBox.editable=false;queueBox.selectable=true;queueBox.verticallyResizable=true;queueBox.horizontallyResizable=false;queueBox.autoresizingMask=$.NSViewWidthSizable;queueBox.textContainer.containerSize=$.NSMakeSize(800,10000000);queueBox.textContainer.widthTracksTextView=true;queueBox.font=$.NSFont.systemFontOfSize(12);queueBox.textContainerInset=$.NSMakeSize(10,10);scroll.documentView=queueBox;view.addSubview(scroll);
    progressBar=$.NSProgressIndicator.alloc.initWithFrame($.NSMakeRect(28,127,824,16));progressBar.indeterminate=false;progressBar.minValue=0;progressBar.maxValue=100;view.addSubview(progressBar);
    statusLabel=label(view,'就绪 · 支持 MP4 / MOV / MKV 等常见视频',28,96,824,24,12,false);
    startButton=button(view,'开始批量导出',24,36,174,5);startButton.keyEquivalent=$('\r');cancelButton=button(view,'停止处理',205,36,116,6);cancelButton.enabled=false;button(view,'打开导出目录',689,36,163,7);
    refresh();win.center;win.makeKeyAndOrderFront(cocoaNil);app.activateIgnoringOtherApps(true);
    timer=$.NSTimer.scheduledTimerWithTimeIntervalTargetSelectorUserInfoRepeats(0.25,controller,'poll:',cocoaNil,true);
    app.run;
}

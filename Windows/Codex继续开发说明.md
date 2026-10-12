# 咸鱼尺寸助手 Windows v1.0.1

这是 x64 Win32 原生 GUI 工具，目标 Windows 10/11 的 Intel/AMD 电脑。exe 内嵌图标、manifest 和首次引擎准备脚本。用户双击即可打开界面，不用 Codex/Python/Node/Visual Studio。GUI 使用系统 common controls，子进程通过 CreateProcessW + CREATE_NO_WINDOW 调用，不经过 cmd.exe。

## 源码

- `app.c`：原生界面、Unicode 文件选择、拖放、任务状态机、进度、停止、报告、原文件保护。
- `video_core.h`：C 滤镜、视频元数据解析、进度解析、Windows 参数引用规则。
- `install_engine.ps1`：从固定 HTTPS 地址获取 Windows FFmpeg；下载包和 exe 双重 SHA-256 校验，只用于每台电脑首次准备。
- `app.rc`、`app.ico`、`app.manifest`：内嵌资源，common-controls v6，asInvoker，DPI 与长路径支持。
- `filters_cli.c`、`test_filters.py`：实际滤镜测试，不等于 Win32 UI 或进程管理验收。
- `验证结果.json`：已执行和未执行项目。

任务状态机：Idle → Probe → Convert → Verify → Save → 下一条。首次缺引擎时先 Install。输出先写临时 MP4，校验尺寸/时长后保存为完成文件；保存前重新检查重名，不覆盖原文件。Job Object 保证停止和退出时结束子进程树。GUI 通过定时器轮询进程和进度文件，避免堵塞管道。

## 构建

本次用 Zig 0.13.0 交叉编译，运行时不需要 Zig。支持在 Windows 安装开发工具后重新编译：

```bash
zig rc /fo app.res /i . app.rc
zig cc -target x86_64-windows-gnu -std=c11 -O2 -municode -Wl,/subsystem:windows app.c app.res -lcomdlg32 -lshell32 -lcomctl32 -lole32 -lgdi32 -luser32 -o LromVideoResizer.exe
```

保持 GUI 子系统为 2；不要生成 Console 子系统导致普通用户打开黑窗口。manifest 请求 asInvoker，避免日常使用索要管理员权限。

测试入口（仅供开发）：

```text
LromVideoResizer.exe --self-test C:\temp\self-test.txt
LromVideoResizer.exe --batch-test C:\tools\ffmpeg.exe C:\temp\outputs 180 320 0 C:\temp\input.mp4
```

`--self-test` 用真实 CommandLineToArgvW 核对中文、空格、反斜杠、引号和空参数，再核对元数据和尺寸解析。`--batch-test` 在隐藏窗口的实际状态机中运行，不是绕过转码流程的替代实现；处理结束退出，失败返回非零。

当前云环境禁止 Windows 兼容运行时所需的 IPC socket，因此 Win32 的 GUI、对话框、CreateProcessW、Job Object、首次 PowerShell 下载和停止操作尚未执行。这一点必须明确，不能把 PE 静态检查或 Linux 的 FFmpeg 测试描述成 Windows 实机测试。

首轮 Windows 实机验收：双击 → 多选 → 拖入 → 中文/空格路径 → 3 种模式 → 有声/无声 → 旋转 → HDR → 同名输出 → 坏文件后继续 → 转码中停止 → 下载中停止 → 退出 → 离线重开。

发布给大量外部用户前，应完成真实 Windows 验收、代码签名和版本管理。若 Smart App Control 或公司策略阻止未签名工具，不要让用户关闭全局安全保护。

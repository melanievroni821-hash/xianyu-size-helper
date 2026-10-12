# 咸鱼尺寸助手 v1.1.0

用户已经在真实 Mac 上确认 v1.0.1 的界面与转码可以使用。这个版本只更换启动入口，让同事从 Finder 双击 app 使用，不输入终端命令。

## 启动链

CFBundleExecutable = lrom-launcher → 原生 arm64 / x86_64 双架构 Mach-O → /bin/bash Resources/launch.sh → /usr/bin/osascript -l JavaScript Resources/app.js Resources目录。

原生入口通过 _NSGetExecutablePath 和 realpath 找到自身位置，支持中文、空格与 app 改名。没有调用 Terminal.app，不把用户路径拼进 shell 命令；内部脚本执行不会要求用户输入。

普通使用：解压、把 app 拖进应用程序、双击。下载得到的 app 仍可能受到 Gatekeeper 拦截，因为没有 Developer ID 签名或 Apple 公证。允许的处理是系统“隐私与安全性 → 仍要打开”；不要以关闭全局安全保护代替签名。

## 源码

- launcher.c：生产原生启动器 C 源码。
- test_launcher.py：真实 C / exec 启动链测试，Linux 使用路径 API shim；不等于 Mac Finder 实测。
- package_native.py：组装双架构 FAT Mach-O，验证入口和 ARM 代码签名页哈希。
- libSystem.tbd：项目自写的最小链接接口声明，不包含 Apple SDK 代码。
- native_validation.json、验证结果.json：验证边界记录。
- app 内 Contents/Resources/app.js：已修复 Cocoa 空值参数的界面、队列和文件管理。
- app 内 Contents/Resources/core.js：转码参数和滤镜；Node 下可测试。
- app 内 Contents/Resources/install_engine.sh：固定 HTTPS 来源与双重 SHA-256 校验。
- app 内 Contents/Resources/launch.sh：本地启动脚本和失败日志。

## 构建与分发

本次用 Zig 0.13.0 交叉编译，用户电脑不用安装 Zig。两种架构分别编译 launcher.c，使用 libSystem.tbd、macOS 12 最低目标；ARM 切片含 Zig 生成的 ad-hoc 签名。Intel 切片未签名。整个 app 尚无 Developer ID 签名或公证。

在真实 Mac 继续开发时，可用 Xcode 命令行工具：

```bash
clang -arch arm64 -mmacosx-version-min=12.0 -Oz launcher.c -o launcher-arm64
clang -arch x86_64 -mmacosx-version-min=12.0 -Oz launcher.c -o launcher-x86_64
lipo -create launcher-arm64 launcher-x86_64 -output lrom-launcher
```

替换 Contents/MacOS/lrom-launcher，保持可执行权限。完成 Developer ID 签名和 Apple 公证后再向大量外部用户发布。ad-hoc 签名不等于苹果公证。

首轮 Mac 验收：新 app 拖到应用程序 → Finder 双击 → 必要时系统放行 → 中文/空格路径 → Dock 重开 → 开始导出 → 缓存引擎离线使用 → 退出。

本次转码核心没有修改。不要以 Linux 的链接或路径测试冒充真实 Mac 的启动验收。

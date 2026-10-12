# 咸鱼尺寸助手

## ⬇️ 下载 Mac / Windows 试用版

**[🍎 下载 Mac 版 ZIP](https://github.com/melanievroni821-hash/xianyu-size-helper/releases/download/v2026.10.12-preview/xianyu-size-helper-mac.zip)** · **[🪟 下载 Windows 版 ZIP](https://github.com/melanievroni821-hash/xianyu-size-helper/releases/download/v2026.10.12-preview/xianyu-size-helper-windows.zip)**

[查看 Release 下载页与版本说明](https://github.com/melanievroni821-hash/xianyu-size-helper/releases/tag/v2026.10.12-preview)

> **这是预发布试用版，当前程序未完成系统发行签名。Windows 实际运行和新版 Mac 原生双击启动尚未实机验证，不能视为已经全面测试。**

Lrom 的本地视频尺寸转换工具。支持批量转换、补黑边、模糊补边、居中裁切、常用尺寸和自定义尺寸。默认导出 1080×1920、30 fps、H.264 MP4；原素材保留。

## 普通用户怎么用

1. 下载对应系统的 ZIP，解压后双击软件：Mac 打开「咸鱼尺寸助手.app」，Windows 打开「咸鱼尺寸助手.exe」。Mac 和 Windows 的应用不能互换。
2. **不需要安装 Codex、Python 或 Node.js。** GitHub 自动提供的 Source code 压缩包是给开发者的，普通用户请选择上方 Mac / Windows ZIP。
3. **首次启动使用时需要联网自动下载转码引擎**，按软件提示完成准备；准备成功后可离线使用。如果首次准备失败，请检查网络后重试。
4. 添加视频，选择目标尺寸和处理模式，再开始导出。**视频在本机处理，原素材保留。** 首次联网用于获取引擎，不上传视频素材。

| 系统 | 支持目标 |
| --- | --- |
| Mac | macOS 12+，Intel 和 Apple 芯片 |
| Windows | Windows 10/11 x64（Intel / AMD） |

上述为支持目标，不代表已经完成所有系统与硬件组合的实机验证。

## 签名与验证边界

- 当前没有 Apple Developer ID 签名与公证，也没有 Windows Authenticode 发行签名；Mac ARM 启动器切片的 ad-hoc 签名不等于系统发行签名或苹果公证。
- 首次打开可能出现系统安全提示。Mac 可在系统「隐私与安全性」中查看提示；如系统或组织策略不允许运行，请遵守其限制，无需关闭全局安全保护。
- 根据源码包内历史记录，此前 Mac JXA 界面与转码流程经用户在 Mac 上验证；**后续新版原生启动器的 Finder 双击启动尚未实机验证**。
- Windows 程序有编译、PE 结构、资源与依赖的历史检查记录；**Windows 界面、启动、首次下载和转码流程尚未实机验证**。
- 平台目录中的 JSON 为随源码保留的历史验证记录，可能对应早期版本或二进制，不是本次 Release 附件的验证证明。Linux FFmpeg 和过滤器测试不能替代 Mac / Windows 实机验收。
- 本次发布检查涵盖 ZIP 完整性、包内结构、源码整理、Mac 压缩包权限及远程上传完整性，不代表软件已全面测试。

macOS 标记版本 1.1.1，Windows 标记版本 1.0.1。视频转换不保证平台审核结果；低清素材放大不能恢复细节。

## 开发与构建

应用代码使用 **MIT 许可证**，见 [LICENSE](LICENSE)。普通用户不需要构建源码。

### macOS

JXA + Cocoa 界面、JavaScript 转码逻辑、C 原生双架构启动器。安装 Xcode Command Line Tools 后，从仓库根目录执行：

```bash
cd macOS
bash build_macos.sh
```

输出：`macOS/dist/咸鱼尺寸助手_Mac.zip`。构建脚本生成 Intel / Apple 芯片通用启动器并设置可执行权限。开发与历史验证说明见 [macOS/源码/Codex继续开发说明.md](macOS/源码/Codex继续开发说明.md)。

### Windows

C / Win32 界面、嵌入式 PowerShell 引擎安装器。安装 Zig 0.13.0 后，从仓库根目录执行：

```powershell
cd Windows
powershell -File build_windows.ps1 -ZigPath zig
```

也可在其他平台用 Python 构建（Python 仅用于开发构建）：

```bash
cd Windows
python3 build_windows.py --zig /path/to/zig
```

输出：`Windows/dist/咸鱼尺寸助手_Windows.zip`。开发与历史验证说明见 [Windows/Codex继续开发说明.md](Windows/Codex继续开发说明.md)。

### 源码与第三方引擎

- `macOS/咸鱼尺寸助手.app` 是构建所需的应用资源模板；原生启动器由构建脚本生成，仓库不提交已编译启动器。
- 用户包只包含一个应用；安装包作为 GitHub Release 附件提供，不提交进源码仓库。
- 不提交下载后的 FFmpeg 引擎、用户缓存、编译中间文件或账号凭证。
- 应用首次使用从 imageio-ffmpeg 0.6.0 的上游 PyPI wheel 获取固定版本引擎，校验下载包和引擎的 SHA-256，存入用户本机缓存。本仓库与用户包不附带 FFmpeg 二进制。
- 应用代码的 MIT 许可证不替代第三方引擎自身的许可证。详见 [Mac 第三方说明](macOS/第三方说明.txt) 与 [Windows 第三方说明](Windows/第三方说明.txt)。分享本工具请发送 Release ZIP，不要附带本机下载的引擎缓存。

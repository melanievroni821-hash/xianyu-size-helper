#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p dist
clang -arch arm64 -mmacosx-version-min=12.0 -Oz 源码/launcher.c -o dist/launcher-arm64
clang -arch x86_64 -mmacosx-version-min=12.0 -Oz 源码/launcher.c -o dist/launcher-x86_64
mkdir -p '咸鱼尺寸助手.app/Contents/MacOS'
lipo -create dist/launcher-arm64 dist/launcher-x86_64 -output '咸鱼尺寸助手.app/Contents/MacOS/lrom-launcher'
chmod 755 '咸鱼尺寸助手.app/Contents/MacOS/lrom-launcher' '咸鱼尺寸助手.app/Contents/Resources/'*.sh
cp ../LICENSE '咸鱼尺寸助手.app/Contents/Resources/LICENSE'
cp 第三方说明.txt '咸鱼尺寸助手.app/Contents/Resources/第三方说明.txt'
ditto -c -k --keepParent '咸鱼尺寸助手.app' 'dist/咸鱼尺寸助手_Mac.zip'
echo '已生成 dist/咸鱼尺寸助手_Mac.zip'

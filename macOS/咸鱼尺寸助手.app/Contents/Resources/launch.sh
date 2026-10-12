#!/bin/bash
set -u
resources="$(cd "$(dirname "$0")" && pwd)"
log_dir="$HOME/Library/Logs/LromVideoResizer"
mkdir -p "$log_dir"
if ! /usr/bin/osascript -l JavaScript "$resources/app.js" "$resources" 2> "$log_dir/启动日志.txt"; then
  /usr/bin/osascript -e 'display alert "尺寸助手启动失败" message "启动日志已保存到用户资源库的 Logs/LromVideoResizer 文件夹，请将启动日志发给 Lrom 排查。" as critical'
  /usr/bin/open "$log_dir"
  exit 1
fi

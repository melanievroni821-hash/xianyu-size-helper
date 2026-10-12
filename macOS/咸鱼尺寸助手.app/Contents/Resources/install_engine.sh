#!/bin/bash
# Download pinned upstream package directly for this user's machine; no Python or package manager required.
set -euo pipefail
engine_dir="$1"
engine_arch="$2"
mkdir -p "$engine_dir"
engine_path="$engine_dir/ffmpeg-$engine_arch"
if [ -x "$engine_path" ]; then exit 0; fi
temp_dir="$(/usr/bin/mktemp -d "$engine_dir/.setup.XXXXXX")"
child_pid=""
cleanup() {
  if [ -n "$child_pid" ]; then /bin/kill "$child_pid" 2>/dev/null || true; fi
  /bin/rm -rf "$temp_dir"
}
trap cleanup EXIT
trap 'exit 130' INT TERM
if [ "$engine_arch" = "arm64" ]; then
  url="https://files.pythonhosted.org/packages/40/5c/f3d8a657d362cc93b81aab8feda487317da5b5d31c0e1fdfd5e986e55d17/imageio_ffmpeg-0.6.0-py3-none-macosx_11_0_arm64.whl"
  wheel_hash="b1ae3173414b5fc5f538a726c4e48ea97edc0d2cdc11f103afee655c463fa742"
  member="imageio_ffmpeg/binaries/ffmpeg-macos-aarch64-v7.1"
  binary_hash="6d175a4743ca50256e89a8cdd731100f9cee33bd79aeea46894d209410dc6617"
else
  url="https://files.pythonhosted.org/packages/da/58/87ef68ac83f4c7690961bce288fd8e382bc5f1513860fc7f90a9c1c1c6bf/imageio_ffmpeg-0.6.0-py3-none-macosx_10_9_intel.macosx_10_9_x86_64.whl"
  wheel_hash="9d2baaf867088508d4a3458e61eeb30e945c4ad8016025545f66c4b5aaef0a61"
  member="imageio_ffmpeg/binaries/ffmpeg-macos-x86_64-v7.1"
  binary_hash="4a4a968b98859588e98500ae25973d80a5ca5eed0724222b9f76360dcb72a001"
fi
wheel="$temp_dir/engine.whl"
printf '%s\n' '正在下载转码引擎（约 21–25MB）…'
/usr/bin/curl --fail --location --proto '=https' --proto-redir '=https' --tlsv1.2 --connect-timeout 20 --max-time 600 --retry 2 --retry-delay 2 --silent --show-error --output "$wheel" "$url" &
child_pid="$!"
wait "$child_pid"
child_pid=""
printf '%s\n' '正在校验下载文件…'
actual_hash="$(/usr/bin/shasum -a 256 "$wheel")"
actual_hash="${actual_hash%% *}"
if [ "$actual_hash" != "$wheel_hash" ]; then printf '%s\n' '下载文件校验失败，未执行引擎。'; exit 2; fi
/usr/bin/unzip -p "$wheel" "$member" > "$temp_dir/ffmpeg"
actual_hash="$(/usr/bin/shasum -a 256 "$temp_dir/ffmpeg")"
actual_hash="${actual_hash%% *}"
if [ "$actual_hash" != "$binary_hash" ]; then printf '%s\n' '引擎校验失败，未执行引擎。'; exit 2; fi
/bin/chmod u+x "$temp_dir/ffmpeg"
"$temp_dir/ffmpeg" -version > "$temp_dir/version.txt" 2>&1 &
child_pid="$!"
wait "$child_pid"
child_pid=""
/usr/bin/unzip -p "$wheel" 'imageio_ffmpeg-0.6.0.dist-info/LICENSE' > "$engine_dir/上游许可证-$engine_arch.txt"
/bin/mv -f "$temp_dir/ffmpeg" "$engine_path"
printf '%s\n' '引擎准备完成。'

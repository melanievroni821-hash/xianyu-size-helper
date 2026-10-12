from pathlib import Path
import argparse, subprocess, zipfile

parser = argparse.ArgumentParser()
parser.add_argument('--zig', default='zig', help='Zig 0.13.0 executable')
args = parser.parse_args()
root = Path(__file__).resolve().parent
out = root / 'dist'
out.mkdir(exist_ok=True)
subprocess.run([args.zig, 'rc', '/fo', 'dist/app.res', '/i', '.', 'app.rc'], cwd=root, check=True)
exe = out / '咸鱼尺寸助手.exe'
subprocess.run([args.zig, 'cc', '-target', 'x86_64-windows-gnu', '-std=c11', '-O2', '-Wall', '-Wextra', '-Wno-unused-function', '-municode', '-Wl,/subsystem:windows', 'app.c', 'dist/app.res', '-lcomdlg32', '-lshell32', '-lcomctl32', '-lole32', '-lgdi32', '-luser32', '-o', str(exe)], cwd=root, check=True)
with zipfile.ZipFile(out / '咸鱼尺寸助手_Windows.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    archive.write(exe, exe.name)
print('已生成', out / '咸鱼尺寸助手_Windows.zip')

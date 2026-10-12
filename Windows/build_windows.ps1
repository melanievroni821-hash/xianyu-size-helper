param([string]$ZigPath = 'zig')
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force -Path 'dist' | Out-Null
    & $ZigPath rc /fo dist/app.res /i . app.rc
    if ($LASTEXITCODE -ne 0) { throw '资源编译失败' }
    & $ZigPath cc -target x86_64-windows-gnu -std=c11 -O2 -Wall -Wextra -Wno-unused-function -municode '-Wl,/subsystem:windows' app.c dist/app.res -lcomdlg32 -lshell32 -lcomctl32 -lole32 -lgdi32 -luser32 -o 'dist/咸鱼尺寸助手.exe'
    if ($LASTEXITCODE -ne 0) { throw '程序编译失败' }
    Compress-Archive -Path 'dist/咸鱼尺寸助手.exe' -DestinationPath 'dist/咸鱼尺寸助手_Windows.zip' -Force
    Write-Host '已生成 dist/咸鱼尺寸助手_Windows.zip'
} finally { Pop-Location }

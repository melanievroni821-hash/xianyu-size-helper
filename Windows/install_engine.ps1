param([Parameter(Mandatory=$true)][string]$CacheDir)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
[IO.Directory]::CreateDirectory($CacheDir) | Out-Null
$wheel = Join-Path $CacheDir '.engine-download.whl'
$partial = Join-Path $CacheDir '.ffmpeg-setup.exe'
$engine = Join-Path $CacheDir 'ffmpeg.exe'
$url = 'https://files.pythonhosted.org/packages/2c/c6/fa760e12a2483469e2bf5058c5faff664acf66cadb4df2ad6205b016a73d/imageio_ffmpeg-0.6.0-py3-none-win_amd64.whl'
try {
    Write-Output 'Downloading verified engine (31 MB)...'
    $client = New-Object Net.WebClient
    try { $client.DownloadFile($url, $wheel) } finally { $client.Dispose() }
    Write-Output 'Verifying downloaded package...'
    if ((Get-FileHash -LiteralPath $wheel -Algorithm SHA256).Hash.ToLowerInvariant() -ne '02fa47c83703c37df6bfe4896aab339013f62bf02c5ebf2dce6da56af04ffc0a') { throw 'Package checksum mismatch; engine was not executed.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($wheel)
    try {
        $entry = $zip.GetEntry('imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe')
        if ($null -eq $entry) { throw 'Expected engine is missing.' }
        $inputStream = $entry.Open()
        $outputStream = [IO.File]::Create($partial)
        try { $inputStream.CopyTo($outputStream) } finally { $outputStream.Dispose(); $inputStream.Dispose() }
    } finally { $zip.Dispose() }
    Write-Output 'Verifying executable...'
    if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant() -ne '2ce797a0f88d7f067180338fb227f7b1928ea727bd9a4d7a1d022f7c52af71a3') { throw 'Executable checksum mismatch; engine was not executed.' }
    & $partial -version | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'The engine cannot run on this computer.' }
    [IO.File]::Copy($partial,$engine,$true)
    Write-Output 'Engine ready. Future video processing works offline.'
} catch {
    Write-Output ('Engine preparation failed: ' + $_.Exception.Message)
    exit 1
} finally {
    Remove-Item -LiteralPath $wheel,$partial -Force -ErrorAction SilentlyContinue
}

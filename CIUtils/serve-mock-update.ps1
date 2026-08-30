<#
.SYNOPSIS
    本地假更新服务器：一键测试整包自动更新链路（无需真实服务端）。

.DESCRIPTION
    1) 用 pack-installer.ps1 打包指定版本的安装器（默认 9.9.9，保证高于任何已装版本）；
    2) 起一个本地 HTTP 服务（TcpListener，无需管理员/URL ACL），提供：
         GET /api/app/get-update  -> {version,url,installerUrl,...} JSON
         GET /<安装器文件名>       -> 安装器 exe 字节
         GET /releases/tag/<ver>  -> 一个 HTML 下载页（冒充 release 页面）
    然后在另一个终端把已安装的旧版 TuneLab 指向本服务启动即可：
         $env:TUNELAB_API_BASE='http://localhost:<port>'; & "$env:LOCALAPPDATA\Programs\TuneLab\TuneLab.exe"
    App 启动即检查到新版 -> 点 Update -> 下载(带进度) -> 退出 -> 安装器覆盖 -> 重启新版。

.PARAMETER Version
    冒充的新版本号，需 > 当前已安装版本。默认 9.9.9。

.PARAMETER Port
    本地服务端口，默认 8000。

.PARAMETER Mode
    模拟的服务端形态，用来覆盖自更新的三条路径：
      normal        —— installerUrl 给安装器直链：走完整自更新（下载→覆盖→重启）。
      legacy        —— 不给 installerUrl（服务端尚未提供该字段）：客户端应退回浏览器打开下载页。
      bad-installer —— installerUrl 指向 HTML 页面（链接指错/CDN 错误页）：客户端应识破并拒绝，
                       提示后打开下载页，绝不把网页交给 shell 执行。

.EXAMPLE
    pwsh CIUtils/serve-mock-update.ps1

.EXAMPLE
    pwsh CIUtils/serve-mock-update.ps1 -Mode bad-installer
#>
[CmdletBinding()]
param(
    [string]$Version = '9.9.9',
    [int]$Port = 8000,
    [string]$Configuration = 'Release',
    [int]$ThrottleKBps = 0,  # >0 时限速发送安装器，便于观察下载进度（KB/s）
    [ValidateSet('normal', 'legacy', 'bad-installer')]
    [string]$Mode = 'normal'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

Write-Host "==> Packing installer $Version…" -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'pack-installer.ps1') -Version $Version -Configuration $Configuration | Out-Host

$installer = Join-Path $repoRoot "TuneLab.Setup\bin\installer\TuneLab-Setup-win-x64-v$Version.exe"
if (-not (Test-Path $installer)) { throw "Installer not found: $installer" }
$installerName  = Split-Path $installer -Leaf
$installerBytes = [System.IO.File]::ReadAllBytes($installer)

# url 是「给人看的下载页」，installerUrl 才是自更新用的安装包直链——两者语义不同，
# 真实服务端的 url 就是 GitHub release 页面，本地也照此模拟。
$pageUrl      = "http://localhost:$Port/releases/tag/v$Version"
$installerUrl = "http://localhost:$Port/$installerName"

$payload = @{
    version     = $Version
    url         = $pageUrl
    description = "# TuneLab $Version`n本地测试更新包。"
    publishedAt = '2026-07-01T00:00:00'
}
switch ($Mode) {
    'normal'        { $payload.installerUrl = $installerUrl }
    'legacy'        { }              # 故意不给：客户端应退回打开 $pageUrl
    'bad-installer' { $payload.installerUrl = $pageUrl }   # 故意指向 HTML：客户端应拒绝执行
}
$json = $payload | ConvertTo-Json -Compress
$jsonBytes = [System.Text.Encoding]::UTF8.GetBytes($json)

$pageHtml = @"
<!DOCTYPE html><html><head><meta charset="utf-8"><title>TuneLab $Version</title></head>
<body><h1>TuneLab $Version</h1><p>Mock release page.</p>
<a href="$installerUrl">$installerName</a></body></html>
"@
$pageBytes = [System.Text.Encoding]::UTF8.GetBytes($pageHtml)

Write-Host ""
Write-Host "Serving mock update at http://localhost:$Port" -ForegroundColor Green
Write-Host ("  version   = {0}" -f $Version)
Write-Host ("  installer = {0} ({1:N1} MB)" -f $installerName, ($installerBytes.Length / 1MB))
Write-Host ("  mode      = {0}" -f $Mode)
switch ($Mode) {
    'legacy'        { Write-Host "  期望行为：点更新后不下载，直接用浏览器打开下载页。" -ForegroundColor Yellow }
    'bad-installer' { Write-Host "  期望行为：下载被拒（非可执行文件），弹提示后打开下载页；临时目录里不留文件。" -ForegroundColor Yellow }
}
Write-Host ""
Write-Host "在另一个终端启动已安装的旧版 TuneLab（版本需 < $Version）：" -ForegroundColor Yellow
Write-Host "  `$env:TUNELAB_API_BASE='http://localhost:$Port'; & `"`$env:LOCALAPPDATA\Programs\TuneLab\TuneLab.exe`""
Write-Host ""
Write-Host "Ctrl+C 停止服务。" -ForegroundColor DarkGray

function Write-Response {
    param($Stream, [string]$Status, [string]$ContentType, [byte[]]$Body)
    $len = if ($Body) { $Body.Length } else { 0 }
    $header = "HTTP/1.1 $Status`r`nContent-Type: $ContentType`r`nContent-Length: $len`r`nConnection: close`r`n`r`n"
    $hb = [System.Text.Encoding]::ASCII.GetBytes($header)
    $Stream.Write($hb, 0, $hb.Length)
    if ($len -gt 0) { $Stream.Write($Body, 0, $Body.Length) }
    $Stream.Flush()
}

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $Port)
$listener.Start()
try {
    while ($true) {
        $client = $listener.AcceptTcpClient()
        try {
            $stream = $client.GetStream()
            $buf = New-Object byte[] 8192
            $n = $stream.Read($buf, 0, $buf.Length)
            if ($n -le 0) { continue }
            $req = [System.Text.Encoding]::ASCII.GetString($buf, 0, $n)
            $firstLine = ($req -split "`r`n")[0]
            $path = ($firstLine -split ' ')[1]
            Write-Host ("  REQ {0}" -f $firstLine) -ForegroundColor DarkGray

            if ($path -like '/api/app/get-update*') {
                Write-Response $stream '200 OK' 'application/json' $jsonBytes
            }
            elseif ($path -like '/releases/tag/*') {
                Write-Response $stream '200 OK' 'text/html; charset=utf-8' $pageBytes
            }
            elseif ($path -like "*/$installerName") {
                if ($ThrottleKBps -gt 0) {
                    $hdr = "HTTP/1.1 200 OK`r`nContent-Type: application/octet-stream`r`nContent-Length: $($installerBytes.Length)`r`nConnection: close`r`n`r`n"
                    $hb = [System.Text.Encoding]::ASCII.GetBytes($hdr)
                    $stream.Write($hb, 0, $hb.Length)
                    $chunk = $ThrottleKBps * 1024
                    $pos = 0
                    while ($pos -lt $installerBytes.Length) {
                        $n = [Math]::Min($chunk, $installerBytes.Length - $pos)
                        $stream.Write($installerBytes, $pos, $n); $stream.Flush()
                        $pos += $n
                        Start-Sleep -Milliseconds 1000
                    }
                }
                else {
                    Write-Response $stream '200 OK' 'application/octet-stream' $installerBytes
                }
            }
            else {
                Write-Response $stream '404 Not Found' 'text/plain' ([byte[]]@())
            }
        }
        catch { Write-Host ("  ERR {0}" -f $_.Exception.Message) -ForegroundColor Red }
        finally { $client.Close() }
    }
}
finally { $listener.Stop() }

# 重新生成用户手册的插图（docs/images/manual/*.png）与快捷键表（docs/generated/keybindings.md）。
#
#   pwsh tools/ScreenshotBot/shoot.ps1            # 输出到 docs/images/manual
#   pwsh tools/ScreenshotBot/shoot.ps1 -Out C:\tmp\shots
#
# 全程不开真窗口（Avalonia headless + 真 Skia 渲染），也不碰你的 %APPDATA%\TuneLab——
# 用户数据整体重定向到一个临时沙盒（见 Sandbox.cs）。TuneLab 正在运行也不影响。
param(
    [string]$Out,
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $here '..\..')).Path

if (-not $Out) { $Out = Join-Path $repo 'docs\images\manual' }

Write-Host "== 构建文档示例插件 ==" -ForegroundColor Cyan
foreach ($name in @('DemoVoice', 'DemoInstrument')) {
    $csproj = Join-Path $here "demo-plugins\$name\$name.csproj"
    # -t:Rebuild：改了 SDK 后 dotnet build 可能判定"无需重建"，留下旧 dll 装进沙盒。
    dotnet build $csproj -c $Configuration -t:Rebuild --nologo -v quiet
    if ($LASTEXITCODE -ne 0) { throw "build failed: $name" }
}

Write-Host "== 构建截图工具 ==" -ForegroundColor Cyan
dotnet build (Join-Path $here 'ScreenshotBot.csproj') -c $Configuration --nologo -v quiet
if ($LASTEXITCODE -ne 0) { throw 'build failed: ScreenshotBot' }

Write-Host "== 渲染插图 -> $Out ==" -ForegroundColor Cyan
$env:TUNELAB_SHOTS_OUT = $Out
dotnet run --project (Join-Path $here 'ScreenshotBot.csproj') -c $Configuration --no-build
if ($LASTEXITCODE -ne 0) { throw 'screenshot run failed' }

Write-Host "完成。" -ForegroundColor Green

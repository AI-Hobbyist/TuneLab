$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$testHost = Join-Path $projectRoot 'out/cubase/m0/ara/bin/Debug/ARATestHost.exe'
$plugin = Join-Path $projectRoot 'out/cubase/m0/native/VST3/Debug/TuneLabBridgeProbe.vst3/Contents/x86_64-win/TuneLabBridgeProbe.vst3'
$logPath = Join-Path $projectRoot 'build.log'

if (!(Test-Path -LiteralPath $testHost) -or !(Test-Path -LiteralPath $plugin)) {
    throw 'Build the SDK test host and TuneLab probes with verify-sdk.ps1 and verify-host.ps1 first.'
}

# EditorView exercises protocol notifications only; no window is created.
& $testHost -vst3 $plugin -test PropertyUpdates ContentUpdates ModificationCloning EditorView Archiving `
    2>&1 | Tee-Object -FilePath $logPath
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) {
    Get-Content $logPath -Tail 40
    exit $buildExitCode
}

param(
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$sdkRoot = Join-Path $projectRoot 'third_party'
$buildRoot = Join-Path $projectRoot 'out/cubase/m0/ara'
$logPath = Join-Path $projectRoot 'build.log'

Push-Location $projectRoot
try {
    & cmake -S "$sdkRoot/ARA_SDK/ARA_Examples" -B $buildRoot `
        -G 'Visual Studio 18 2026' -A x64 `
        "-DARA_VST3_SDK_DIR=$sdkRoot/vst3sdk" `
        -DARA_SETUP_DEBUGGING=OFF -DSMTG_CREATE_PLUGIN_LINK=OFF `
        2>&1 | Tee-Object -FilePath $logPath
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -ne 0) {
        Get-Content $logPath -Tail 40
        exit $buildExitCode
    }

    & cmake --build $buildRoot --config $Configuration `
        --target ARATestPlugInVST3 ARATestHost --parallel 1 -- /nodeReuse:false `
        2>&1 | Tee-Object -FilePath $logPath -Append
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -ne 0) {
        Get-Content $logPath -Tail 40
        exit $buildExitCode
    }

    $binaryRoot = Join-Path $buildRoot "bin/$Configuration"
    & "$binaryRoot/ARATestHost.exe" -vst3 "$binaryRoot/ARATestPlugIn.vst3" `
        -test ContentReading PlaybackRendering `
        2>&1 | Tee-Object -FilePath $logPath -Append
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -ne 0) {
        Get-Content $logPath -Tail 40
        exit $buildExitCode
    }
}
finally {
    Pop-Location
}

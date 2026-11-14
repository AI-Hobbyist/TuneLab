param(
    [Parameter(Mandatory = $true)][string]$StatePath,
    [Parameter(Mandatory = $true)][string]$DataDirectory
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$stateFile = (Resolve-Path -LiteralPath $StatePath).Path
$dataRoot = (Resolve-Path -LiteralPath $DataDirectory).Path
$nativeRoot = Join-Path $projectRoot 'out/cubase/m0/native'
$probe = Join-Path $nativeRoot 'Debug/tunelab_processor_probe.exe'
$plugin = Join-Path $nativeRoot 'VST3/Debug/TuneLabInstrumentProbe.vst3/Contents/x86_64-win/TuneLabInstrumentProbe.vst3'
if (-not (Test-Path -LiteralPath $probe) -or -not (Test-Path -LiteralPath $plugin)) {
    throw 'Build the native targets with verify-host.ps1 before running the processor probe.'
}

$previousDataDirectory = $env:TUNELAB_DATA_DIR
try {
    $env:TUNELAB_DATA_DIR = $dataRoot
    & $probe $plugin $stateFile 2>&1 | Tee-Object -FilePath (Join-Path $projectRoot 'build.log')
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -ne 0) {
        Get-Content (Join-Path $projectRoot 'build.log') -Tail 40
        throw "Processor verification failed with exit code $buildExitCode."
    }
    Copy-Item -LiteralPath (Join-Path $projectRoot 'build.log') `
        -Destination (Join-Path $projectRoot 'processor-verification.log') -Force
}
finally {
    $env:TUNELAB_DATA_DIR = $previousDataDirectory
}

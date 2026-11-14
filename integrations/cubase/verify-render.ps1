param(
    [Parameter(Mandatory = $true)][string]$StatePath,
    [Parameter(Mandatory = $true)][string]$DataDirectory,
    [int]$SampleRate = 44100,
    [Nullable[int]]$PreviewPitch = $null
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$stateFile = (Resolve-Path -LiteralPath $StatePath).Path
$dataRoot = (Resolve-Path -LiteralPath $DataDirectory).Path
$probe = Join-Path $projectRoot 'out/cubase/m0/native/Debug/tunelab_render_probe.exe'
$managedDirectory = Join-Path $PSScriptRoot 'managed/bin/Debug/net8.0'
if (-not (Test-Path -LiteralPath $probe)) {
    throw 'Build the native targets with verify-host.ps1 before running the render probe.'
}

# Use an explicitly selected data directory; do not change the user's installed
# extension settings or distribute their voicebank with this verification.
$previousDataDirectory = $env:TUNELAB_DATA_DIR
try {
    $env:TUNELAB_DATA_DIR = $dataRoot
    $probeArguments = @($managedDirectory, $stateFile, $SampleRate)
    if ($null -ne $PreviewPitch) {
        if ($PreviewPitch -lt 0 -or $PreviewPitch -gt 127) {
            throw 'PreviewPitch must be in the MIDI range 0..127.'
        }
        $probeArguments += $PreviewPitch
    }
    & $probe @probeArguments 2>&1 |
        Tee-Object -FilePath (Join-Path $projectRoot 'build.log')
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -ne 0) {
        Get-Content (Join-Path $projectRoot 'build.log') -Tail 40
        throw "Render verification failed with exit code $buildExitCode."
    }
    Copy-Item -LiteralPath (Join-Path $projectRoot 'build.log') `
        -Destination (Join-Path $projectRoot 'render-verification.log') -Force
}
finally {
    $env:TUNELAB_DATA_DIR = $previousDataDirectory
}

param(
    [string]$DotNetRoot = 'C:/Program Files/dotnet',
    [string]$HostPackVersion = '8.0.6',
    [string]$HostFxrVersion = '8.0.26',
    [string]$AraProjectState = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$buildRoot = Join-Path $projectRoot 'out/cubase/m0/native'
$managedProject = Join-Path $PSScriptRoot 'managed/TuneLab.Cubase.Managed.csproj'
$managedOutput = Join-Path $PSScriptRoot 'managed/bin/Debug/net8.0'
$includePath = "$DotNetRoot/packs/Microsoft.NETCore.App.Host.win-x64/$HostPackVersion/runtimes/win-x64/native"
$logPath = Join-Path $projectRoot 'build.log'

function Invoke-Checked {
    param([string]$Command, [string[]]$Arguments)

    & $Command @Arguments 2>&1 | Tee-Object -FilePath $logPath -Append
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -ne 0) {
        Get-Content $logPath -Tail 40
        throw "Command failed with exit code ${buildExitCode}: $Command"
    }
}

Set-Content -LiteralPath $logPath -Value 'TuneLab native/managed host verification'
Invoke-Checked 'dotnet' @('build', $managedProject, '-c', 'Debug')
Invoke-Checked 'cmake' @(
    '-S', "$PSScriptRoot/native", '-B', $buildRoot,
    '-G', 'Visual Studio 18 2026', '-A', 'x64',
    "-DDOTNET_HOST_INCLUDE=$includePath"
)
Invoke-Checked 'cmake' @(
    '--build', $buildRoot, '--config', 'Debug', '--parallel', '1',
    '--', '/nodeReuse:false'
)
Invoke-Checked "$buildRoot/Debug/tunelab_host_probe.exe" @(
    "$DotNetRoot/host/fxr/$HostFxrVersion/hostfxr.dll",
    "$managedOutput/TuneLab.Cubase.Managed.runtimeconfig.json",
    "$managedOutput/TuneLab.Cubase.Managed.dll"
)
Invoke-Checked "$buildRoot/Debug/tunelab_state_probe.exe" @()
Invoke-Checked "$buildRoot/Debug/tunelab_view_protocol_probe.exe" @()
Invoke-Checked "$buildRoot/Debug/tunelab_audio_cache_probe.exe" @()
Invoke-Checked "$buildRoot/Debug/tunelab_midi_input_probe.exe" @()
Invoke-Checked "$buildRoot/Debug/tunelab_host_time_probe.exe" @()
Invoke-Checked "$buildRoot/Debug/tunelab_ara_archive_probe.exe" @()
if ($AraProjectState) {
    $araStatePath = (Resolve-Path -LiteralPath $AraProjectState).Path
    Invoke-Checked "$buildRoot/Debug/tunelab_ara_state_probe.exe" @($araStatePath)
}
Invoke-Checked "$buildRoot/Debug/tunelab_instrument_parameters_probe.exe" @()
Invoke-Checked "$buildRoot/Debug/tunelab_midi_preview_player_probe.exe" @()
Invoke-Checked "$buildRoot/Debug/tunelab_runtime_probe.exe" @($managedOutput)
foreach ($pluginName in @('TuneLabInstrumentProbe', 'TuneLabBridgeProbe')) {
    $bundleManaged = "$buildRoot/VST3/Debug/$pluginName.vst3/Contents/Resources/managed"
    Invoke-Checked "$buildRoot/Debug/tunelab_runtime_probe.exe" @($bundleManaged)
}

$instrumentManaged = "$buildRoot/VST3/Debug/TuneLabInstrumentProbe.vst3/Contents/Resources/managed"
$bridgeManaged = "$buildRoot/VST3/Debug/TuneLabBridgeProbe.vst3/Contents/Resources/managed"
Invoke-Checked "$buildRoot/Debug/tunelab_runtime_probe.exe" @(
    $instrumentManaged, '--peer', $bridgeManaged
)

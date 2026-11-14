param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$HostPackVersion = '8.0.26',
    [string]$HostPackRoot = '',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$buildRoot = Join-Path $projectRoot 'out/cubase/release/native'
$logPath = Join-Path $projectRoot 'build.log'
if (-not $HostPackRoot) {
    $nugetRoot = if ($env:NUGET_PACKAGES) { $env:NUGET_PACKAGES } else {
        Join-Path ([Environment]::GetFolderPath('UserProfile')) '.nuget/packages'
    }
    $HostPackRoot = Join-Path $nugetRoot "microsoft.netcore.app.host.win-x64/$HostPackVersion"
}
$hostNative = Join-Path $HostPackRoot 'runtimes/win-x64/native'
foreach ($required in @('runtimes/win-x64/native/nethost.dll', 'LICENSE.TXT', 'THIRD-PARTY-NOTICES.TXT',
    'microsoft.netcore.app.host.win-x64.nuspec')) {
    if (-not (Test-Path -LiteralPath (Join-Path $HostPackRoot $required))) {
        throw "The specified Windows x64 host NuGet package is incomplete: $required"
    }
}
$hostMetadata = [xml](Get-Content -LiteralPath (Join-Path $HostPackRoot 'microsoft.netcore.app.host.win-x64.nuspec') -Raw)
if ($hostMetadata.package.metadata.id -ne 'Microsoft.NETCore.App.Host.win-x64' -or
    $hostMetadata.package.metadata.version -ne $HostPackVersion) {
    throw 'The host NuGet package identity/version differs from the requested package.'
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot ('out/cubase/packages/rc-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$packageRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $packageRoot) {
    throw "Use a new output directory; existing packages are preserved: $packageRoot"
}
New-Item -ItemType Directory -Path $packageRoot | Out-Null
$publishRoot = Join-Path $packageRoot 'managed-publish'
$payload = Join-Path $packageRoot 'TuneLab-Cubase-win-x64'
New-Item -ItemType Directory -Path $payload | Out-Null

function Invoke-Checked {
    param([string]$Command, [string[]]$Arguments)

    & $Command @Arguments 2>&1 | Tee-Object -FilePath $logPath -Append
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -ne 0) {
        Get-Content -LiteralPath $logPath -Tail 60
        throw "Command failed with exit code ${buildExitCode}: $Command"
    }
}

Set-Content -LiteralPath $logPath -Value 'TuneLab Cubase Windows x64 candidate packaging'
Invoke-Checked 'dotnet' @(
    'publish', "$PSScriptRoot/managed/TuneLab.Cubase.Managed.csproj",
    '-c', $Configuration, '-r', 'win-x64', '--self-contained', 'false',
    '-o', $publishRoot, '-m:1', '/nodeReuse:false'
)
Invoke-Checked 'cmake' @(
    '-S', "$PSScriptRoot/native", '-B', $buildRoot,
    '-G', 'Visual Studio 18 2026', '-A', 'x64', "-DDOTNET_HOST_INCLUDE=$hostNative"
)
Invoke-Checked 'cmake' @(
    '--build', $buildRoot, '--config', $Configuration,
    '--parallel', '1', '--', '/nodeReuse:false'
)

foreach ($plugin in @('TuneLabInstrumentProbe', 'TuneLabBridgeProbe')) {
    $sourceBundle = Join-Path $buildRoot "VST3/$Configuration/$plugin.vst3"
    $targetBundle = Join-Path $payload "$plugin.vst3"
    # Use the RID-specific publish below instead of the development managed copy.
    foreach ($file in Get-ChildItem -LiteralPath $sourceBundle -File -Recurse) {
        $relative = [IO.Path]::GetRelativePath($sourceBundle, $file.FullName)
        if ($relative.Replace('\', '/').StartsWith('Contents/Resources/managed/')) {
            continue
        }
        $destination = Join-Path $targetBundle $relative
        New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination
    }
    $managed = Join-Path $targetBundle 'Contents/Resources/managed'
    New-Item -ItemType Directory -Force -Path $managed | Out-Null
    Get-ChildItem -LiteralPath $publishRoot | Copy-Item -Destination $managed -Recurse
    Copy-Item -LiteralPath (Join-Path $hostNative 'nethost.dll') -Destination $managed -Force
}

$licenses = Join-Path $payload 'licenses'
New-Item -ItemType Directory -Path $licenses | Out-Null
Copy-Item -LiteralPath "$projectRoot/LICENSE.txt", "$projectRoot/THIRD-PARTY-NOTICES.txt" -Destination $payload
foreach ($entry in @(
    @('third_party/vst3sdk/LICENSE.txt', 'VST3-LICENSE.txt'),
    @('third_party/vst3sdk/base/LICENSE.txt', 'VST3-base-LICENSE.txt'),
    @('third_party/vst3sdk/pluginterfaces/LICENSE.txt', 'VST3-interfaces-LICENSE.txt'),
    @('third_party/vst3sdk/public.sdk/LICENSE.txt', 'VST3-public-sdk-LICENSE.txt'),
    @('third_party/ARA_SDK/ARA_API/LICENSE.txt', 'ARA-API-LICENSE.txt'),
    @('third_party/ARA_SDK/ARA_Library/LICENSE.txt', 'ARA-Library-LICENSE.txt')
)) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $entry[0]) -Destination (Join-Path $licenses $entry[1])
}
Copy-Item -LiteralPath (Join-Path $HostPackRoot 'LICENSE.TXT') -Destination (Join-Path $licenses 'dotnet-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $HostPackRoot 'THIRD-PARTY-NOTICES.TXT') -Destination (Join-Path $licenses 'dotnet-THIRD-PARTY-NOTICES.txt')
Copy-Item -LiteralPath "$PSScriptRoot/README.md" -Destination (Join-Path $payload 'README.md')
Copy-Item -LiteralPath "$PSScriptRoot/fixtures" -Destination (Join-Path $payload 'fixtures') -Recurse
Copy-Item -LiteralPath "$projectRoot/docs/cubase-manual-checklist.md" -Destination $payload
Copy-Item -LiteralPath "$projectRoot/docs/cubase-dependencies.md" -Destination $payload
$revision = & git -C $projectRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot record the source revision.' }
$dirty = [bool](& git -C $projectRoot status --porcelain --untracked-files=no)
if ($LASTEXITCODE -ne 0) { throw 'Cannot record the source worktree state.' }
$dependencies = Get-Content -LiteralPath (Join-Path $publishRoot 'TuneLab.Cubase.Managed.deps.json') -Raw | ConvertFrom-Json
$libraries = @($dependencies.libraries.PSObject.Properties | Sort-Object Name | ForEach-Object {
    [ordered]@{ id = $_.Name; type = $_.Value.type }
})
$nativeLibraries = @(foreach ($sdk in @(
    @('VST3 SDK', 'third_party/vst3sdk', 'MIT'),
    @('ARA API', 'third_party/ARA_SDK/ARA_API', 'Apache-2.0'),
    @('ARA Library', 'third_party/ARA_SDK/ARA_Library', 'Apache-2.0')
)) {
    $sdkRevision = & git -C (Join-Path $projectRoot $sdk[1]) rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw "Cannot record SDK revision: $($sdk[0])" }
    [ordered]@{ name = $sdk[0]; revision = $sdkRevision.Trim(); license = $sdk[2] }
})
$files = @(Get-ChildItem -LiteralPath $payload -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = [IO.Path]::GetRelativePath($payload, $_.FullName).Replace('\', '/')
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        bytes = $_.Length
    }
})
[ordered]@{
    format = 1
    platform = 'win-x64'
    configuration = $Configuration
    sourceRevision = $revision.Trim()
    sourceTrackedDirty = $dirty
    runtime = 'Microsoft.NETCore.App 8.0 x64 (installed separately)'
    nethostPack = "Microsoft.NETCore.App.Host.win-x64/$HostPackVersion"
    managedLibraries = $libraries
    nativeLibraries = $nativeLibraries
    manualValidation = 'SKIPPED by user; Cubase native lyric delivery is unverified'
    files = $files
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $payload 'manifest.json') -Encoding utf8
Compress-Archive -LiteralPath $payload -DestinationPath (Join-Path $packageRoot 'TuneLab-Cubase-win-x64.zip')
Write-Output "Candidate package: $payload"
Write-Output 'Packaging does not install plugins or launch Cubase.'

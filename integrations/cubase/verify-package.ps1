param(
    [Parameter(Mandatory)]
    [string]$PackageDirectory,
    [string]$ProbeDirectory = '',
    [string]$ValidatorPath = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$logPath = Join-Path $projectRoot 'build.log'
if (-not $ProbeDirectory) {
    $ProbeDirectory = Join-Path $projectRoot 'out/cubase/release/native/Release'
}
if (-not $ValidatorPath) {
    $ValidatorPath = Join-Path $projectRoot 'out/cubase/release/native/bin/Release/validator.exe'
}

function Require-File {
    param([string]$Relative)
    if (-not (Test-Path -LiteralPath (Join-Path $package $Relative) -PathType Leaf)) {
        throw "Missing packaged file: $Relative"
    }
}

function Invoke-Checked {
    param([string]$Command, [string[]]$Arguments)
    & $Command @Arguments 2>&1 | Tee-Object -FilePath $logPath -Append
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -ne 0) {
        Get-Content -LiteralPath $logPath -Tail 60
        throw "Package check failed with exit code ${buildExitCode}: $Command"
    }
}

Set-Content -LiteralPath $logPath -Value 'TuneLab relocated candidate package verification (no GUI)'
Require-File 'manifest.json'
$manifest = Get-Content -LiteralPath (Join-Path $package 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.format -ne 1 -or $manifest.platform -ne 'win-x64' -or $manifest.files.Count -eq 0) {
    throw 'Unsupported or empty package manifest.'
}
$listed = @{}
foreach ($entry in $manifest.files) {
    $relative = [string]$entry.path
    $absolute = [IO.Path]::GetFullPath((Join-Path $package $relative))
    if (-not $absolute.StartsWith($package + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Manifest path leaves package: $relative"
    }
    if ($listed.ContainsKey($relative)) { throw "Duplicate manifest path: $relative" }
    Require-File $relative
    $file = Get-Item -LiteralPath $absolute
    if ($file.Length -ne $entry.bytes -or (Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Package checksum mismatch: $relative"
    }
    $listed[$relative] = $true
}
foreach ($file in Get-ChildItem -LiteralPath $package -File -Recurse) {
    $relative = [IO.Path]::GetRelativePath($package, $file.FullName).Replace('\', '/')
    if ($relative -ne 'manifest.json' -and -not $listed.ContainsKey($relative)) {
        throw "Unlisted package file: $relative"
    }
    if ($file.Extension -in @('.so', '.dylib', '.onnx', '.pth', '.pt', '.ckpt')) {
        throw "Unexpected non-Windows runtime or model file: $relative"
    }
}
foreach ($license in @('LICENSE.txt', 'THIRD-PARTY-NOTICES.txt', 'licenses/VST3-LICENSE.txt',
    'licenses/ARA-API-LICENSE.txt', 'licenses/ARA-Library-LICENSE.txt',
    'licenses/VST3-base-LICENSE.txt', 'licenses/VST3-interfaces-LICENSE.txt',
    'licenses/VST3-public-sdk-LICENSE.txt', 'licenses/dotnet-LICENSE.txt',
    'licenses/dotnet-THIRD-PARTY-NOTICES.txt', 'README.md')) {
    Require-File $license
}
foreach ($fixture in @('fixtures/tempo-lyrics.mid', 'fixtures/tempo-lyrics.tlpx',
    'fixtures/tempo-lyrics.state', 'fixtures/README.md', 'cubase-manual-checklist.md',
    'cubase-dependencies.md')) {
    Require-File $fixture
}
$managedDirectories = @()
foreach ($plugin in @('TuneLabInstrumentProbe', 'TuneLabBridgeProbe')) {
    $bundle = "$plugin.vst3"
    $native = "$bundle/Contents/x86_64-win/$plugin.vst3"
    Require-File $native
    Require-File "$bundle/Contents/Resources/moduleinfo.json"
    $bytes = [IO.File]::ReadAllBytes((Join-Path $package $native))
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x4550 -or
        [BitConverter]::ToUInt16($bytes, $peOffset + 4) -ne 0x8664) {
        throw "Plugin is not a Windows x64 PE image: $plugin"
    }
    $managed = "$bundle/Contents/Resources/managed"
    foreach ($file in @('TuneLab.Cubase.Managed.dll', 'TuneLab.Cubase.Managed.deps.json',
        'TuneLab.Cubase.Managed.runtimeconfig.json', 'TuneLab.dll', 'TuneLab.SDK.dll',
        'TuneLab.Foundation.dll', 'TuneLab.Hosting.Compat.Legacy.dll', 'nethost.dll',
        'Resources/Manual/zh-CN.md')) {
        Require-File "$managed/$file"
    }
    $config = Get-Content -LiteralPath (Join-Path $package "$managed/TuneLab.Cubase.Managed.runtimeconfig.json") -Raw | ConvertFrom-Json
    if ($config.runtimeOptions.framework.name -ne 'Microsoft.NETCore.App' -or
        $config.runtimeOptions.framework.version -notlike '8.0.*') {
        throw 'Package runtime requirement differs from its installation instructions.'
    }
    $deps = Get-Content -LiteralPath (Join-Path $package "$managed/TuneLab.Cubase.Managed.deps.json") -Raw | ConvertFrom-Json
    $target = $deps.targets.PSObject.Properties | Where-Object Name -eq $deps.runtimeTarget.name
    if (-not $target) { throw 'The published dependency target is missing.' }
    foreach ($library in $target.Value.PSObject.Properties) {
        foreach ($kind in @('runtime', 'native')) {
            foreach ($asset in $library.Value.$kind.PSObject.Properties) {
                Require-File "$managed/$([IO.Path]::GetFileName($asset.Name))"
            }
        }
    }
    $managedDirectories += Join-Path $package $managed
    Invoke-Checked $ValidatorPath @((Join-Path $package $bundle))
}
$fixtureRoot = Join-Path $projectRoot ('out/cubase/m6/protocol-host-' + [Guid]::NewGuid().ToString('N'))
$fixtureBinary = Join-Path $fixtureRoot 'Contents/x86_64-win'
$fixtureManaged = Join-Path $fixtureRoot 'Contents/Resources/managed'
New-Item -ItemType Directory -Force -Path $fixtureBinary, $fixtureManaged | Out-Null
# The controller fixture resolves resources relative to its own executable.
# Supply an exact package copy there, never a development managed directory.
Copy-Item -LiteralPath (Join-Path $ProbeDirectory 'tunelab_runtime_probe.exe') -Destination $fixtureBinary
Get-ChildItem -LiteralPath $managedDirectories[0] | Copy-Item -Destination $fixtureManaged -Recurse
Invoke-Checked (Join-Path $fixtureBinary 'tunelab_runtime_probe.exe') @(
    $managedDirectories[0], '--peer', $managedDirectories[1]
)
Write-Output "PASS: $($manifest.files.Count) checksummed files, two Windows x64 bundles, required resources/licenses, validators and cross-bundle CLR protocol."
Write-Output 'Cubase/GUI/install/listening validation remains SKIPPED by user.'

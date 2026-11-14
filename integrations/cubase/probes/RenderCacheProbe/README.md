# Real singer render cache probe

This Windows command-line probe uses an explicitly supplied local TuneLab project-state file and its installed voice. It never opens a GUI, modifies the input file, installs a voice, or distributes a model. Use an isolated TUNELAB_DATA_DIR already configured with the test engine and singer. The current authorized local singer is fu2_ning2_na4.

The probe creates two separated phrases, renders them through CubaseRenderSession, changes only the first lyric, and checks that the second phrase retains its raw AudioSegment, CommitVersion, sample buffer, output samples and waveform references. It also checks finite nonzero changed audio and compares the retained render against a cold render with a maximum error threshold of 0.00001. Failure to obtain at least two real synthesis segments is a failure, not a skipped cache check. This is engine/session evidence; it does not verify Cubase's native lyric event delivery or pronunciation by listening.

The executable intentionally uses the existing TuneLab.Tests friend assembly identity to inspect internal synthesis objects without changing the public SDK or adding a production diagnostic API. Run it separately from the xUnit test assembly.

From the repository root, build with the required foreground logging:

```powershell
& dotnet build integrations/cubase/probes/RenderCacheProbe/RenderCacheProbe.csproj -c Debug -m:1 /nodeReuse:false 2>&1 | Tee-Object -FilePath build.log
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) { Get-Content build.log -Tail 60; exit $buildExitCode }
```

Then run against the local test setup (paths are examples from this workspace):

```powershell
$env:TUNELAB_DATA_DIR = Join-Path $PWD 'out/cubase/m2/data'
& dotnet integrations/cubase/probes/RenderCacheProbe/bin/Debug/net8.0/TuneLab.Tests.dll out/cubase/m2/fu2_ning2_na4.state 2>&1 | Tee-Object -FilePath build.log
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) { Get-Content build.log -Tail 60; exit $buildExitCode }
```

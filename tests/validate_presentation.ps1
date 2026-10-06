param(
  [Parameter(Mandatory=$true)][string]$BinaryDirectory,
  [Parameter(Mandatory=$true)][string]$Rom,
  [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')][string]$RunLabel,
  [ValidateSet('Disabled','4:3','16:9','21:9','32:9','Fit')][string]$Aspect = 'Disabled',
  [ValidateSet('Disabled','Auto','60','90','120','144','165','240','360')][string]$Fps = 'Disabled',
  [string]$ModState,
  [string]$RouteFile,
  [string]$CompareRun,
  [string]$WindowSize = 'Auto',
  [switch]$AllowRendererFallback,
  [switch]$Paced,
  [switch]$Screenshots,
  [int]$ScreenshotFrom = 1350,
  [int]$ScreenshotTo = 1760,
  [int]$MaximumFrames = 2300
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$validationRoot = Join-Path $repositoryRoot 'build-validation'
$binaryRoot = (Resolve-Path -LiteralPath $BinaryDirectory).Path
$romPath = (Resolve-Path -LiteralPath $Rom).Path
if (!$RouteFile) { $RouteFile = Join-Path $PSScriptRoot 'presentation_route.txt' }
$routeSource = (Resolve-Path -LiteralPath $RouteFile).Path
if ($ModState) {
  if ($PSBoundParameters.ContainsKey('Aspect') -or $PSBoundParameters.ContainsKey('Fps')) {
    throw 'Use either -ModState or -Aspect/-Fps.'
  }
  $modStateSource = (Resolve-Path -LiteralPath $ModState).Path
}
if ($CompareRun) { $compareRoot = (Resolve-Path -LiteralPath $CompareRun).Path }
if ($MaximumFrames -lt 1) { throw '-MaximumFrames must be positive.' }
if ($ScreenshotFrom -gt $ScreenshotTo) { throw 'Screenshot range is reversed.' }
if ($WindowSize -notmatch '^(Auto|[1-9][0-9]*x[1-9][0-9]*)$') { throw 'WindowSize must be Auto or WidthxHeight.' }
$runRoot = Join-Path $validationRoot $RunLabel
if (Test-Path -LiteralPath $runRoot) { throw "Run directory already exists: $runRoot" }
$exeSource = Join-Path $binaryRoot 'DoomSNESRecomp.exe'
$dllSource = Join-Path $binaryRoot 'SDL3.dll'
foreach ($required in @($exeSource,$dllSource)) {
  if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required build artifact missing: $required" }
}
New-Item -ItemType Directory -Path $runRoot | Out-Null
$dumpRoot = Join-Path $runRoot 'dumps'
$presentRoot = Join-Path $runRoot 'presents'
$catalogRoot = Join-Path $runRoot 'mods/preloaded'
foreach ($directory in @($dumpRoot,$presentRoot,$catalogRoot)) {
  New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
Copy-Item -LiteralPath $exeSource,$dllSource -Destination $runRoot
$route = Join-Path $runRoot 'route.txt'
Copy-Item -LiteralPath $routeSource -Destination $route
# Copy the build's staged packages, never its mutable state or saves. A stock
# baseline build may predate the package; use the repository package then.
$packagesSource = Join-Path $binaryRoot 'mods/preloaded/packages'
$presentationManifest = Join-Path $packagesSource 'doom.presentation/1.0.0/manifest.toml'
if (!(Test-Path -LiteralPath $presentationManifest)) {
  $packagesSource = Join-Path $repositoryRoot 'mods/preloaded/packages'
}
if (Test-Path -LiteralPath $packagesSource) {
  Copy-Item -LiteralPath $packagesSource -Destination $catalogRoot -Recurse
}
if (($Aspect -ne 'Disabled' -or $Fps -ne 'Disabled' -or $ModState) -and
    !(Test-Path -LiteralPath (Join-Path $catalogRoot 'packages/doom.presentation/1.0.0/manifest.toml'))) {
  throw 'The Doom presentation package must be staged beside the build or present in the repository.'
}
$statePath = Join-Path $catalogRoot 'state.toml'
if ($ModState) {
  Copy-Item -LiteralPath $modStateSource -Destination $statePath
} else {
  $wideEnabled = if ($Aspect -eq 'Disabled') { 'false' } else { 'true' }
  $fpsEnabled = if ($Fps -eq 'Disabled') { 'false' } else { 'true' }
  $aspectValue = if ($Aspect -eq 'Disabled') { 'Fit' } else { $Aspect }
  $fpsValue = if ($Fps -eq 'Disabled') { 'Auto' } else { $Fps }
  $stateText = @"
format_version = 1

[[package]]
id = "doom.presentation"
version = "1.0.0"

[[feature]]
package_id = "doom.presentation"
id = "widescreen"
enabled = $wideEnabled

[feature.values]
aspect = "$aspectValue"

[[feature]]
package_id = "doom.presentation"
id = "presentation-fps"
enabled = $fpsEnabled

[feature.values]
fps = "$fpsValue"
"@
  Set-Content -LiteralPath $statePath -Value $stateText -Encoding ASCII
}
Copy-Item -LiteralPath $statePath -Destination (Join-Path $runRoot 'requested-mod-state.toml')
$frameDelay = if ($Paced) { '0' } else { '1' }
$configText = @"
[General]
Autosave = 0
DisableFrameDelay = $frameDelay
RunAhead = 0
DisplayPerfInTitle = 0

[Graphics]
WindowSize = $WindowSize
Fullscreen = 0
WindowScale = 3
OutputMethod = SDL-Software
NewRenderer = 1
IgnoreAspectRatio = 0
NoSpriteLimits = 1
FrameBlend = 0
VSync = Off

[Sound]
EnableAudio = 0
"@
Set-Content -LiteralPath (Join-Path $runRoot 'config.ini') -Value $configText -Encoding ASCII
# Clear inherited presentation/capture knobs that could contaminate a run;
# restore all process environment values after the isolated child exits.
$environment = @{
  SDL_VIDEODRIVER = 'dummy'
  SDL_AUDIODRIVER = 'dummy'
  SNESRECOMP_DUMP_DIR = $dumpRoot
  SNESRECOMP_HOST_PROFILE = '1'
  SNESRECOMP_HOST_PROFILE_START_FRAME = '1358'
  SNESRECOMP_RUN_FRAMES = [string]$MaximumFrames
  SNESRECOMP_FRAME_TIMING = (Join-Path $runRoot 'frame-timing.csv')
  SNESRECOMP_STATE_TRACE = (Join-Path $runRoot 'state-trace.csv')
  SNESRECOMP_PRESENT_LOG = (Join-Path $runRoot 'presents.csv')
  SNESRECOMP_SCREENSHOT = $null
  SNESRECOMP_SCREENSHOT_FRAME = $null
  SNESRECOMP_SCREENSHOT_DIR = $null
  SNESRECOMP_SCREENSHOT_FROM = $null
  SNESRECOMP_SCREENSHOT_TO = $null
  SNESRECOMP_FRAME_BLEND = $null
  SNESRECOMP_RUNAHEAD = '0'
  SNESRECOMP_REWIND = '0'
  SNESRECOMP_OSD_FPS = '0'
  DOOM_RENDER_STATS = '1'
}
if ($Screenshots) {
  $environment.SNESRECOMP_SCREENSHOT_DIR = $presentRoot
  $environment.SNESRECOMP_SCREENSHOT_FROM = [string]$ScreenshotFrom
  $environment.SNESRECOMP_SCREENSHOT_TO = [string]$ScreenshotTo
}
$previousEnvironment = @{}
try {
  foreach ($name in $environment.Keys) {
    $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
    [Environment]::SetEnvironmentVariable($name,$environment[$name],'Process')
  }
  $exe = Join-Path $runRoot 'DoomSNESRecomp.exe'
  $arguments = @('--no-launcher','--script',('"' + $route + '"'),'--rom',('"' + $romPath + '"'))
  $timer = [Diagnostics.Stopwatch]::StartNew()
  $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $runRoot -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $runRoot 'stdout.log') -RedirectStandardError (Join-Path $runRoot 'stderr.log')
  $timer.Stop()
  $summary = [ordered]@{
    binary = $exe
    binarySha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    route = $route
    routeSha256 = (Get-FileHash -LiteralPath $route -Algorithm SHA256).Hash
    romSha256 = (Get-FileHash -LiteralPath $romPath -Algorithm SHA256).Hash
    requestedModStateSha256 = (Get-FileHash -LiteralPath (Join-Path $runRoot 'requested-mod-state.toml') -Algorithm SHA256).Hash
    aspect = $Aspect
    fps = $Fps
    windowSize = $WindowSize
    paced = [bool]$Paced
    screenshots = [bool]$Screenshots
    allowRendererFallback = [bool]$AllowRendererFallback
    elapsedSeconds = $timer.Elapsed.TotalSeconds
    exitCode = $process.ExitCode
    architecturalBrkLines = @(Select-String -LiteralPath (Join-Path $runRoot 'stderr.log') -Pattern 'architectural BRK').Count
    rendererFailureLines = @(Select-String -LiteralPath (Join-Path $runRoot 'stderr.log') -Pattern '\[doom-replay-failed\]').Count
  }
  $rendererLines = @(Select-String -LiteralPath (Join-Path $runRoot 'stderr.log') -Pattern '\[doom-render\].*passes=')
  foreach ($metric in @('supported','captured','passes','cached','failures')) {
    $values = @($rendererLines | ForEach-Object {
      if ($_.Line -match ('\b' + $metric + '=(\d+)')) { [uint64]$Matches[1] }
    })
    $maximum = if ($values.Count) { ($values | Measure-Object -Maximum).Maximum } else { $null }
    $summary['renderer_' + $metric] = $maximum
  }
  $hashes = Get-ChildItem -LiteralPath $dumpRoot -File | Where-Object { $_.Extension -in @('.bin','.json','.bgrx') } | Sort-Object Name | ForEach-Object {
    [pscustomobject]@{ file=$_.Name; bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
  }
  $hashes | Export-Csv -LiteralPath (Join-Path $runRoot 'dump-hashes.csv') -NoTypeInformation
  $dumpFrames = Get-ChildItem -LiteralPath $dumpRoot -Filter '*.info.json' -File | Sort-Object Name | ForEach-Object {
    $info = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
    [pscustomobject]@{ tag=$_.Name.Replace('.info.json',''); frame=$info.frame; masterClock=$info.master_clock; vpos=$info.vpos; hpos=$info.hpos }
  }
  $dumpFrames | Export-Csv -LiteralPath (Join-Path $runRoot 'dump-frames.csv') -NoTypeInformation
  $pauseSnapshot = Join-Path $dumpRoot 'pause.wram.bin'
  $resumeSnapshot = Join-Path $dumpRoot 'resumed.wram.bin'
  if ((Test-Path -LiteralPath $pauseSnapshot) -and (Test-Path -LiteralPath $resumeSnapshot)) {
    $pauseBytes = [IO.File]::ReadAllBytes($pauseSnapshot)
    $resumeBytes = [IO.File]::ReadAllBytes($resumeSnapshot)
    # Retail RLFlags at WRAM $2B uses bit $4000 for game-not-halted.
    $pauseFlags = [BitConverter]::ToUInt16($pauseBytes,0x2b)
    $resumeFlags = [BitConverter]::ToUInt16($resumeBytes,0x2b)
    $summary.pauseFlags = '0x{0:X4}' -f $pauseFlags
    $summary.resumeFlags = '0x{0:X4}' -f $resumeFlags
    $summary.pauseVerified = (($pauseFlags -band 0x4000) -eq 0 -and ($resumeFlags -band 0x4000) -ne 0)
  }
  if ($CompareRun) {
    $referenceSummary = Get-Content -LiteralPath (Join-Path $compareRoot 'run-summary.json') -Raw | ConvertFrom-Json
    if ($referenceSummary.routeSha256 -ne $summary.routeSha256 -or $referenceSummary.romSha256 -ne $summary.romSha256) {
      throw 'Comparison requires identical route and ROM digests.'
    }
    $referenceHashes = Import-Csv -LiteralPath (Join-Path $compareRoot 'dump-hashes.csv')
    $referenceGuest = @($referenceHashes | Where-Object { $_.file -notmatch '\.fb\.' })
    $actualGuest = @($hashes | Where-Object { $_.file -notmatch '\.fb\.' })
    $differences = @(Compare-Object -ReferenceObject $referenceGuest -DifferenceObject $actualGuest -Property file,bytes,sha256)
    $referenceTrace = (Get-FileHash -LiteralPath (Join-Path $compareRoot 'state-trace.csv') -Algorithm SHA256).Hash
    $actualTrace = (Get-FileHash -LiteralPath (Join-Path $runRoot 'state-trace.csv') -Algorithm SHA256).Hash
    $comparison = [ordered]@{ reference=$compareRoot; guestDumpDifferences=$differences.Count; stateTraceMatches=($referenceTrace -eq $actualTrace); differences=$differences }
    $comparison | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runRoot 'comparison.json') -Encoding ASCII
    $summary.guestDumpDifferences = $differences.Count
    $summary.stateTraceMatches = $referenceTrace -eq $actualTrace
  }
  $summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'run-summary.json') -Encoding ASCII
  $summary | ConvertTo-Json
  Get-Content -LiteralPath (Join-Path $runRoot 'stderr.log') -Tail 30
  if ($process.ExitCode) { throw "Run exited $($process.ExitCode)" }
  if (!(Select-String -LiteralPath (Join-Path $runRoot 'stderr.log') -Pattern 'exit: script quit' -Quiet)) { throw 'Route did not reach its final quit command.' }
  if ($summary.Contains('pauseVerified') -and !$summary.pauseVerified) { throw 'Pause/resume did not change the native game-not-halted flag as expected.' }
  if (($Aspect -ne 'Disabled' -or $Fps -ne 'Disabled') -and !$AllowRendererFallback -and
      ($summary.renderer_supported -ne 1 -or $summary.renderer_passes -le 0 -or $summary.renderer_failures -ne 0 -or $summary.rendererFailureLines -ne 0)) {
    throw 'Enhanced gameplay renderer did not execute successfully; inspect renderer counters in run-summary.json and stderr.log. Use -AllowRendererFallback only for intentional unsupported/non-gameplay routes.'
  }
  if ($CompareRun -and ($summary.guestDumpDifferences -ne 0 -or !$summary.stateTraceMatches)) { throw 'Presentation changed guest state; inspect comparison.json.' }
} finally {
  foreach ($name in $previousEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name,$previousEnvironment[$name],'Process') }
}

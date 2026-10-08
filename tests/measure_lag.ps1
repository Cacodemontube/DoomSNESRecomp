param(
  [string]$BinaryDirectory = './build',
  [string]$Rom = 'C:/Roms/Doom (USA).sfc',
  [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')][string]$RunLabel,
  [switch]$Enhancements,
  [switch]$WidescreenOnly,
  [switch]$InterpolationOnly,
  [ValidateSet('Auto','60','90','120','144','165','240','360')][string]$Fps = 'Auto',
  [switch]$Paced,
  [switch]$Desktop,
  [switch]$Audio,
  [switch]$Capture,
  [switch]$StateDumps,
  [string]$RouteFile,
  [int]$DebugPort = 0,
  [int]$CaptureFrom = 1480,
  [int]$CaptureTo = 1600
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$source = (Resolve-Path $BinaryDirectory).Path
$root = Join-Path $repo ('build-lag-evidence/' + $RunLabel)
if (Test-Path $root) { throw "Run exists: $root" }
New-Item -ItemType Directory $root | Out-Null
Copy-Item (Join-Path $source 'DoomSNESRecomp.exe') $root
Get-ChildItem $source -Filter '*.dll' | Copy-Item -Destination $root
New-Item -ItemType Directory (Join-Path $root 'mods/preloaded') -Force | Out-Null
Copy-Item (Join-Path $source 'mods/preloaded/packages') (Join-Path $root 'mods/preloaded') -Recurse
if ($Enhancements -or $WidescreenOnly -or $InterpolationOnly) {
  $wide = if ($Enhancements -or $WidescreenOnly) { 'true' } else { 'false' }
  $interpolate = if ($Enhancements -or $InterpolationOnly) { 'true' } else { 'false' }
  $state = @"
format_version = 1
[[package]]
id = "doom.presentation"
version = "1.0.0"
[[feature]]
package_id = "doom.presentation"
id = "widescreen"
enabled = $wide
[feature.values]
aspect = "Fit"
[[feature]]
package_id = "doom.presentation"
id = "presentation-fps"
enabled = $interpolate
[feature.values]
fps = "$Fps"
"@
  Set-Content (Join-Path $root 'mods/preloaded/state.toml') $state -Encoding ascii
} else {
  Set-Content (Join-Path $root 'mods/preloaded/state.toml') "format_version = 1" -Encoding ascii
}
$config = Get-Content (Join-Path $repo 'build/config.ini') -Raw
$config = $config -replace '(?m)^SkipLauncher\s*=\s*[^\r\n]*','SkipLauncher = 1'
$config = $config -replace '(?m)^Autosave\s*=\s*[^\r\n]*','Autosave = 0'
$config = $config -replace '(?m)^DisableFrameDelay\s*=\s*[^\r\n]*', ('DisableFrameDelay = ' + [int](!$Paced))
$config = $config -replace '(?m)^EnableAudio\s*=\s*[^\r\n]*', ('EnableAudio = ' + [int][bool]$Audio)
$output = if ($Desktop) { 'SDL' } else { 'SDL-Software' }
$config = $config -replace '(?m)^OutputMethod\s*=\s*[^\r\n]*', ('OutputMethod = ' + $output)
$config = $config -replace '(?m)^VSync\s*=\s*[^\r\n]*','VSync = 0'
# Script input is the sole input source in an isolated measurement.
$config = $config -replace '(?m)^SourceP1\s*=\s*[^\r\n]*','SourceP1 = 0'
$config = $config -replace '(?m)^SourceP2\s*=\s*[^\r\n]*','SourceP2 = 0'
Set-Content (Join-Path $root 'config.ini') $config -Encoding ascii
$routeSource = if ($RouteFile) { (Resolve-Path $RouteFile).Path } else { Join-Path $PSScriptRoot 'presentation_route.txt' }
$route = Get-Content $routeSource | Where-Object { $StateDumps -or $_ -notmatch '^dump ' }
Set-Content (Join-Path $root 'route.txt') $route -Encoding ascii
$envs = @{
  SNESRECOMP_FRAME_TIMING = (Join-Path $root 'timing.csv')
  SNESRECOMP_HOST_PROFILE_START_FRAME = '1358'
  SNESRECOMP_RUN_FRAMES = '2300'
  SNESRECOMP_DEBUG_PORT = $(if ($DebugPort -gt 0) { [string]$DebugPort } else { $null })
  SNESRECOMP_RUNAHEAD = '0'
  SNESRECOMP_REWIND = '0'
  SNESRECOMP_PRESENT_LOG = $null
  SNESRECOMP_STATE_TRACE = $(if ($StateDumps) { Join-Path $root 'state-trace.csv' } else { $null })
  SNESRECOMP_DUMP_DIR = $(if ($StateDumps) { Join-Path $root 'dumps' } else { $null })
  DOOM_RENDER_STATS = '1'
  SNESRECOMP_SCREENSHOT_DIR = $(if ($Capture) { Join-Path $root 'captures' } else { $null })
  SNESRECOMP_SCREENSHOT_FROM = [string]$CaptureFrom
  SNESRECOMP_SCREENSHOT_TO = [string]$CaptureTo
  SDL_VIDEODRIVER = $(if ($Desktop) { $null } else { 'dummy' })
  SDL_AUDIODRIVER = $(if ($Audio) { $null } else { 'dummy' })
}
$previous = @{}
if ($Capture) { New-Item -ItemType Directory (Join-Path $root 'captures') | Out-Null }
if ($StateDumps) { New-Item -ItemType Directory (Join-Path $root 'dumps') | Out-Null }
try {
  foreach ($key in $envs.Keys) {
    $previous[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
    [Environment]::SetEnvironmentVariable($key, $envs[$key], 'Process')
  }
  $args = @('--no-launcher','--script',('"' + (Join-Path $root 'route.txt') + '"'),'--rom',('"' + $Rom + '"'))
  $process = Start-Process (Join-Path $root 'DoomSNESRecomp.exe') -ArgumentList $args -WorkingDirectory $root -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $root 'stdout.log') -RedirectStandardError (Join-Path $root 'stderr.log')
  Write-Output "Run $RunLabel PID=$($process.Id) root=$root"
  $process.WaitForExit()
  if ($process.ExitCode -ne 0) { throw "Game exit: $($process.ExitCode)" }
  $rows = @(Import-Csv (Join-Path $root 'timing.csv') | Where-Object { [int]$_.frame -ge 1400 -and [int]$_.frame -le 1900 })
  if ($rows.Count -lt 2) { throw 'Route did not reach the measured gameplay field window (1400-1900).' }
  $intervals = for ($i=1; $i -lt $rows.Count; $i++) { 1000 * ([double]$rows[$i].present_seconds - [double]$rows[$i-1].present_seconds) }
  $sorted = @($intervals | Sort-Object)
  $summary = [ordered]@{ run=$RunLabel; enhancements=[bool]$Enhancements; widescreenOnly=[bool]$WidescreenOnly; interpolationOnly=[bool]$InterpolationOnly; requestedFps=$Fps; captures=[bool]$Capture; paced=[bool]$Paced; desktop=[bool]$Desktop; audio=[bool]$Audio; samples=$rows.Count; binarySha256=(Get-FileHash (Join-Path $root 'DoomSNESRecomp.exe')).Hash; romSha256=(Get-FileHash $Rom).Hash; meanIntervalMs=($intervals | Measure-Object -Average).Average; p95Ms=$sorted[[int](($sorted.Count-1)*0.95)]; p99Ms=$sorted[[int](($sorted.Count-1)*0.99)]; maxMs=($intervals | Measure-Object -Maximum).Maximum }
  foreach ($key in @('guest_ms','raster_ms','compose_ms','present_ms','hook_ms','wait_ms','guest_periods')) { $summary[$key] = ($rows | ForEach-Object { [double]$_.$key } | Measure-Object -Average).Average }
  $summary | ConvertTo-Json | Set-Content (Join-Path $root 'summary.json')
  $summary | ConvertTo-Json
} finally {
  foreach ($key in $previous.Keys) { [Environment]::SetEnvironmentVariable($key, $previous[$key], 'Process') }
}

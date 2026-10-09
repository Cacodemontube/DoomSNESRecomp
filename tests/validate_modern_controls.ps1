param(
  [string]$BinaryDirectory = './build-lag-debug',
  [string]$Rom = './Doom (USA).sfc',
  [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')][string]$RunLabel,
  [ValidateSet(1,2,3,4)][int]$ResolutionScale = 1,
  [ValidateSet('4:3','16:9','32:9')][string]$Aspect = '4:3',
  [int]$DebugPort = 4395
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$source = (Resolve-Path -LiteralPath $BinaryDirectory).Path
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$root = Join-Path $repo ('build-lag-evidence/' + $RunLabel)
if (Test-Path -LiteralPath $root) { throw "Run exists: $root" }
New-Item -ItemType Directory -Path (Join-Path $root 'mods/preloaded') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'DoomSNESRecomp.exe') -Destination $root
Get-ChildItem $source -Filter '*.dll' | Copy-Item -Destination $root
Copy-Item -LiteralPath (Join-Path $source 'mods/preloaded/packages') -Destination (Join-Path $root 'mods/preloaded') -Recurse
$wide = if ($Aspect -ne '4:3') { 'true' } else { 'false' }
$resolution = if ($ResolutionScale -gt 1) { 'true' } else { 'false' }
$state = @"
format_version = 1
[[package]]
id = "doom.presentation"
version = "1.0.0"
[[feature]]
package_id = "doom.presentation"
id = "modern-controls"
enabled = true
[[feature]]
package_id = "doom.presentation"
id = "widescreen"
enabled = $wide
[feature.values]
aspect = "$Aspect"
[[feature]]
package_id = "doom.presentation"
id = "render-resolution"
enabled = $resolution
[feature.values]
scale = "$ResolutionScale"
"@
Set-Content -LiteralPath (Join-Path $root 'mods/preloaded/state.toml') -Value $state -Encoding ascii
$config = [IO.File]::ReadAllText((Join-Path $repo 'build/config.ini'))
foreach ($setting in @('SkipLauncher','DisableFrameDelay')) {
  $config = $config -replace ('(?m)^'+$setting+'\s*=\s*[^\r\n]*'), ($setting+' = 1')
}
foreach ($setting in @('Autosave','EnableAudio','SourceP1','SourceP2','RunAhead')) {
  $config = $config -replace ('(?m)^'+$setting+'\s*=\s*[^\r\n]*'), ($setting+' = 0')
}
$config = $config -replace '(?m)^OutputMethod\s*=\s*[^\r\n]*','OutputMethod = SDL-Software'
$config = $config -replace '(?m)^VSync\s*=\s*[^\r\n]*','VSync = 0'
Set-Content -LiteralPath (Join-Path $root 'config.ini') -Value $config -Encoding ascii
$route = @"
wait 400
press start 1
wait 150
press start 1
wait 150
press a 1
wait 150
press a 1
wait 1100
quit
"@
Set-Content -LiteralPath (Join-Path $root 'route.txt') -Value $route -Encoding ascii
$envs = @{ SDL_VIDEODRIVER='dummy';SDL_AUDIODRIVER='dummy';SNESRECOMP_DEBUG_PORT=[string]$DebugPort;
  SNESRECOMP_RUN_FRAMES='2200';SNESRECOMP_RUNAHEAD='0';SNESRECOMP_REWIND='0';DOOM_RENDER_STATS='1' }
$previous = @{}
$process = $null; $client = $null
try {
  foreach ($key in $envs.Keys) {
    $previous[$key] = [Environment]::GetEnvironmentVariable($key,'Process')
    [Environment]::SetEnvironmentVariable($key,$envs[$key],'Process')
  }
  $arguments = @('--no-launcher','--script',('"'+(Join-Path $root 'route.txt')+'"'),'--rom',('"'+$romPath+'"'))
  $process = Start-Process -FilePath (Join-Path $root 'DoomSNESRecomp.exe') -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'stdout.log') -RedirectStandardError (Join-Path $root 'stderr.log')
  $processHandle = $process.Handle # retain the native handle for ExitCode after polling
  $deadline = [DateTime]::UtcNow.AddSeconds(30)
  do {
    $client = [Net.Sockets.TcpClient]::new()
    try { $client.Connect('127.0.0.1',$DebugPort) } catch { $client.Dispose(); $client=$null; Start-Sleep -Milliseconds 100 }
    if ($process.HasExited) { throw 'Game exited before TCP startup; see stderr.log.' }
  } while (!$client -and [DateTime]::UtcNow -lt $deadline)
  if (!$client) { throw 'TCP startup timed out.' }
  $stream = $client.GetStream();$stream.ReadTimeout=10000
  $reader = [IO.StreamReader]::new($stream)
  $writer = [IO.StreamWriter]::new($stream);$writer.AutoFlush=$true
  $transcript = [Collections.Generic.List[object]]::new()
  function Query([string]$command) {
    $writer.WriteLine($command)
    $line = $reader.ReadLine()
    if (!$line) { throw "TCP closed after $command" }
    $result = $line | ConvertFrom-Json
    $transcript.Add([ordered]@{command=$command;result=$result})
    if ($result.error) { throw "$command : $($result.error)" }
    return $result
  }
  $null = Query 'run_to_frame 1400'
  $deadline = [DateTime]::UtcNow.AddSeconds(60)
  do { Start-Sleep -Milliseconds 100; $start=Query 'game input_stats' } while ($start.frame -lt 1400 -and [DateTime]::UtcNow -lt $deadline)
  if (!$start.enabled -or !$start.gameplay -or !$start.movement_updates) { throw 'Native modern-controls hook did not reach gameplay.' }
  Write-Output "Native controls hook active at field $($start.frame)."
  $before = Query 'read_sram 5a8e 38'
  $null = Query ('screenshot '+((Join-Path $root 'center.bmp') -replace '\\','/'))
  $null = Query 'set_controller l'
  $null = Query 'game mouse_motion_test 24 -120'
  $null = Query 'step 24'
  $after = Query 'read_sram 5a8e 38'
  $up = Query 'game input_stats'
  if ($up.mouse_turns -lt 1 -or $up.horizon_offset -ne 30) { throw 'Mouse turn and look-up request were not applied.' }
  if ($before.hex -eq $after.hex) { throw 'Strafe and turn did not change the player state.' }
  $beforeBytes = @($before.hex -split ' ' | ForEach-Object { [Convert]::ToInt32($_,16) })
  $afterBytes = @($after.hex -split ' ' | ForEach-Object { [Convert]::ToInt32($_,16) })
  $beforeAngle = $beforeBytes[20] + 256*$beforeBytes[21]
  $afterAngle = $afterBytes[20] + 256*$afterBytes[21]
  if ($afterAngle -ne (($beforeAngle-24*64) -band 65534)) { throw 'Native player angle did not use the original mouse scale.' }
  if (($beforeBytes[6..13] -join ',') -eq ($afterBytes[6..13] -join ',')) { throw 'Player did not strafe while mouse turning.' }
  $null = Query 'clear_controller'
  $null = Query ('screenshot '+((Join-Path $root 'look-up.bmp') -replace '\\','/'))
  $null = Query 'game mouse_motion_test 0 256'
  $null = Query 'step 12'
  $down = Query 'game input_stats'
  if ($down.horizon_offset -ne -34) { throw 'Look-down request was not applied.' }
  $null = Query ('screenshot '+((Join-Path $root 'look-down.bmp') -replace '\\','/'))
  $render = Query 'game presentation_stats'
  if (!$render.supported -or $render.failures -ne 0 -or !$render.resolution_segments) { throw 'Classic-look renderer failed.' }
  $summary = [ordered]@{resolutionScale=$ResolutionScale;aspect=$Aspect;input=$down;renderer=$render;nativeBefore=$before;nativeAfter=$after}
  $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'summary.json')
  $null = Query 'continue'
  if (!$process.WaitForExit(30000)) { throw 'Game did not finish the isolated route.' }
  if ($process.ExitCode -ne 0) { throw "Game exit: $($process.ExitCode)" }
  $transcript | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'tcp.json')
  $summary | ConvertTo-Json -Depth 5
} finally {
  if ($client) { $client.Dispose() }
  if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id }
  foreach ($key in $previous.Keys) { [Environment]::SetEnvironmentVariable($key,$previous[$key],'Process') }
}

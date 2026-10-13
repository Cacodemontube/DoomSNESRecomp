param(
  [string]$BinaryDirectory = './build-lag-debug',
  [string]$Rom = './Doom (USA).sfc',
  [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')][string]$RunLabel,
  [ValidateSet(1,2,3,4)][int]$ResolutionScale = 1,
  [ValidateSet('4:3','16:9','32:9')][string]$Aspect = '4:3',
  [ValidateRange(0,4)][int]$RunAhead = 0,
  [int]$DebugPort = 4395,
  [switch]$Interpolated,
  [switch]$Projectiles
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
$fps=if($Interpolated){'true'}else{'false'}
$state = @"
format_version = 1
[[package]]
id = "doom.presentation"
version = "1.0.0"
[[feature]]
package_id = "doom.presentation"
id = "modern-controls"
enabled = true
[feature.values]
vertical-look = "on"
jumping = "on"
crouching = "on"
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
[[feature]]
package_id = "doom.presentation"
id = "presentation-fps"
enabled = $fps
[feature.values]
fps = "120"
[[package]]
id = "doom.cheats"
version = "1.0.0"
[[feature]]
package_id = "doom.cheats"
id = "keyboard-cheats"
enabled = true
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
wait 12000
quit
"@
Set-Content -LiteralPath (Join-Path $root 'route.txt') -Value $route -Encoding ascii
$envs = @{ SDL_VIDEODRIVER='dummy';SDL_AUDIODRIVER='dummy';SNESRECOMP_DEBUG_PORT=[string]$DebugPort;
  SNESRECOMP_RUN_FRAMES='14000';SNESRECOMP_RUNAHEAD=[string]$RunAhead;SNESRECOMP_REWIND='0';DOOM_RENDER_STATS='1' }
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
  function Word([string]$address) {
    $dump=Query ('read_sram '+$address+' 2')
    $bytes=@($dump.hex -split ' ' | ForEach-Object { [Convert]::ToInt32($_,16) })
    return $bytes[0]+256*$bytes[1]
  }
  if($Projectiles) {
    $null=Query 'game cheat_test IDDQD';$null=Query 'step 12'
    $null=Query 'game cheat_test IDFA';$null=Query 'step 30'
    $null=Query 'game modern_key_test 5';$null=Query 'step 60'
    if((Word '1dc') -ne 10){throw 'Rocket launcher not equipped'}
    $null=Query 'game cheat_test IDDT';$null=Query 'step 12'
    $null=Query 'game cheat_test IDDT';$null=Query 'step 12'
    # Native SNES Doom does not fire the player's gun while the map is open.
    $null=Query 'set_controller y';$null=Query 'step 100'
    $null=Query 'set_controller select';$null=Query 'step 36';$null=Query 'clear_controller'
    $null=Query 'read_sram 5a8e 3800'
    $null=Query 'game cheat_stats';$null=Query 'read_sram 1e6 2'
    $peak=0;$seen=$false
    for($i=0;$i -lt 80;$i++) {
      $null=Query 'step 3';$stats=Query 'game presentation_stats'
      $peak=[Math]::Max($peak,[int]$stats.automap_projectile_arrows)
      if($stats.automap_projectile_arrows -gt 0 -and !$seen){
        $null=Query ('screenshot '+((Join-Path $root 'projectile-map.bmp') -replace '\\','/'));$seen=$true
      }
    }
    $null=Query 'clear_controller'
    if(!$seen){throw 'No separately captured native projectile arrow'}
    Write-Output ('Native automap projectile arrows passed; maximum='+$peak)
  } else {
    $previousStats=Query 'game presentation_stats';$held=0;$latched=0;$changed=0
    for($i=0;$i -lt 80;$i++) {
      $null=Query 'game mouse_motion_test 0 1';$null=Query 'step 1'
      $stats=Query 'game presentation_stats'
      if($stats.captures -eq $previousStats.captures -and $stats.has_snapshot){
        if(!$Interpolated -and $stats.look_offset -ne $previousStats.look_offset){throw 'Original pitch changed between native captures'}
        $held++
      } elseif($stats.captures -gt $previousStats.captures) { $latched++ }
      if($stats.look_offset -ne $previousStats.look_offset){$changed++}
      $previousStats=$stats
    }
    if(!$held -or !$latched -or !$changed){throw 'Look cadence did not exercise both held and updated frames'}
    $null=Query ('screenshot '+((Join-Path $root 'look-up.bmp') -replace '\\','/'))
    $null=Query 'game mouse_motion_test 0 -128';$null=Query 'step 40'
    $stats=Query 'game presentation_stats'
    if($stats.look_offset -ne $stats.requested_look_offset){throw 'Pitch did not catch up at a native capture'}
    Write-Output ('Look cadence passed: held fields='+$held+' native captures='+$latched+' visible changes='+$changed+' interpolated='+$Interpolated)
  }
} finally {
  if($transcript) {$transcript|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $root 'tcp.json')}
  if ($client) { $client.Dispose() }
  if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id }
  foreach ($key in $previous.Keys) { [Environment]::SetEnvironmentVariable($key,$previous[$key],'Process') }
}



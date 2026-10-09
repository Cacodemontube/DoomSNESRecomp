param(
  [string]$BinaryDirectory='./build-lag-debug',
  [string]$Rom='./Doom (USA).sfc',
  [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')][string]$RunLabel,
  [int]$DebugPort=4396,
  [ValidateSet(1,2,3,4)][int]$Scale=2,
  [ValidateSet('4:3','16:9','21:9','32:9')][string]$Aspect='16:9',
  [switch]$Native,
  [switch]$Automap,
  [switch]$Transparent,
  [switch]$Interpolate,
  [switch]$Menu,
  [switch]$Deep,
  [string[]]$Warps=@('31','32','33','38','11')
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$source=(Resolve-Path -LiteralPath $BinaryDirectory).Path
$romPath=(Resolve-Path -LiteralPath $Rom).Path
$root=Join-Path $repo ('build-lag-evidence/'+$RunLabel)
if(Test-Path -LiteralPath $root) { throw "Run exists: $root" }
New-Item -ItemType Directory -Path (Join-Path $root 'mods/preloaded') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'DoomSNESRecomp.exe') -Destination $root
Copy-Item -LiteralPath (Join-Path $source 'mods/preloaded/packages') -Destination (Join-Path $root 'mods/preloaded') -Recurse
$config=Get-Content -LiteralPath (Join-Path $repo 'build/config.ini') -Raw
foreach($setting in @('Autosave','EnableAudio','DisableFrameDelay','SourceP1','SourceP2','VSync')) {
  $config=$config -replace ('(?m)^'+$setting+'\s*=\s*[^\r\n]*'),($setting+' = 0')
}
$config=$config -replace '(?m)^OutputMethod\s*=\s*[^\r\n]*','OutputMethod = SDL-Software'
Set-Content -LiteralPath (Join-Path $root 'config.ini') -Value $config -Encoding ascii
Set-Content -LiteralPath (Join-Path $root 'mods/preloaded/state.toml') -Encoding ascii -Value ((@'
format_version = 1
[[package]]
id = "doom.presentation"
version = "1.0.0"
[[feature]]
package_id = "doom.presentation"
id = "widescreen"
enabled = true
[feature.values]
aspect = "ASPECT_VALUE"
[[feature]]
package_id = "doom.presentation"
id = "render-resolution"
enabled = true
[feature.values]
scale = "SCALE_VALUE"
[[feature]]
package_id = "doom.presentation"
id = "transparent-automap"
enabled = TRANSPARENT_VALUE
[[feature]]
package_id = "doom.presentation"
id = "presentation-fps"
enabled = INTERPOLATE_VALUE
[feature.values]
fps = "60"
[[package]]
id = "doom.cheats"
version = "1.0.0"
[[feature]]
package_id = "doom.cheats"
id = "keyboard-cheats"
enabled = true
'@).Replace('ASPECT_VALUE',$Aspect).Replace('SCALE_VALUE',[string]$Scale).Replace('TRANSPARENT_VALUE',([string][bool]$Transparent).ToLower()).Replace('INTERPOLATE_VALUE',([string][bool]$Interpolate).ToLower()))
if($Native -or $Scale -eq 1) {
  $statePath=Join-Path $root 'mods/preloaded/state.toml'
  $state=Get-Content -LiteralPath $statePath -Raw
  $features=if($Native){'render-resolution|widescreen'}else{'render-resolution'}
  $state=$state -replace ('(id = "(?:'+$features+')"\s+enabled = )true'),'${1}false'
  Set-Content -LiteralPath $statePath -Value $state -Encoding ascii
}
Set-Content -LiteralPath (Join-Path $root 'route.txt') -Encoding ascii -Value @'
wait 400
press start 1
wait 150
press start 1
wait 150
press a 1
wait 150
press a 1
wait 16000
quit
'@
$envs=@{SDL_VIDEODRIVER='dummy';SDL_AUDIODRIVER='dummy';SNESRECOMP_DEBUG_PORT=[string]$DebugPort;
  SNESRECOMP_RUN_FRAMES='18000';SNESRECOMP_RUNAHEAD='0';SNESRECOMP_REWIND='0'}
$previous=@{};$process=$null;$client=$null;$transcript=[Collections.Generic.List[object]]::new()
try {
  foreach($key in $envs.Keys) {
    $previous[$key]=[Environment]::GetEnvironmentVariable($key,'Process')
    [Environment]::SetEnvironmentVariable($key,$envs[$key],'Process')
  }
  $arguments=@('--no-launcher','--script',('"'+(Join-Path $root 'route.txt')+'"'),'--rom',('"'+$romPath+'"'))
  $process=Start-Process -FilePath (Join-Path $root 'DoomSNESRecomp.exe') -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'stdout.log') -RedirectStandardError (Join-Path $root 'stderr.log')
  $deadline=[DateTime]::UtcNow.AddSeconds(30)
  do {
    $client=[Net.Sockets.TcpClient]::new()
    try {$client.Connect('127.0.0.1',$DebugPort)} catch {$client.Dispose();$client=$null;Start-Sleep -Milliseconds 100}
    if($process.HasExited) {throw 'Game exited before TCP startup.'}
  } while(!$client -and [DateTime]::UtcNow -lt $deadline)
  if(!$client) {throw 'TCP startup timed out.'}
  $stream=$client.GetStream();$stream.ReadTimeout=30000
  $reader=[IO.StreamReader]::new($stream);$writer=[IO.StreamWriter]::new($stream);$writer.AutoFlush=$true
  function Query([string]$command) {
    $writer.WriteLine($command);$line=$reader.ReadLine()
    if(!$line) {throw "TCP closed after $command"}
    $result=$line|ConvertFrom-Json;$transcript.Add(@{command=$command;result=$result})
    if($result.error) {throw "$command : $($result.error)"};return $result
  }
  $null=Query 'run_to_frame 1400'
  do {Start-Sleep -Milliseconds 100;$s=Query 'game cheat_stats'} while($s.frame -lt 1400)
  if(!$s.ready -or !$s.enabled) {throw 'Cheat hooks did not reach gameplay.'}
  foreach($warp in $Warps) {
    $null=Query ('game cheat_test IDCLEV'+$warp);$null=Query 'step 240'
    $null=Query 'game cheat_test IDDQD';$null=Query 'step 12'
    if($Automap) {
      $null=Query 'game cheat_test IDDT';$null=Query 'step 12'
      $null=Query 'set_controller select';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 36'
    }
    $null=Query 'read_sram 7c 24'
    $null=Query 'read_sram 7180 10416'
    $null=Query ('screenshot '+((Join-Path $root ($warp+'-start.bmp')) -replace '\\','/'))
    $null=Query 'set_controller up';$null=Query 'step 120';$null=Query 'clear_controller'
    $null=Query ('screenshot '+((Join-Path $root ($warp+'-forward.bmp')) -replace '\\','/'))
    if(!$Automap -and $warp -in @('31','38')) {
      $null=Query 'set_controller a';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 120'
      $null=Query 'set_controller up';$null=Query 'step 180';$null=Query 'clear_controller'
      $null=Query ('screenshot '+((Join-Path $root ($warp+'-open.bmp')) -replace '\\','/'))
    }
    $null=Query 'set_controller right';$null=Query 'step 24';$null=Query 'clear_controller'
    $null=Query ('screenshot '+((Join-Path $root ($warp+'-turn.bmp')) -replace '\\','/'))
    $stats=Query 'game presentation_stats'
    if($Deep -and $warp -eq '31') {
      $null=Query 'set_controller left';$null=Query 'step 24';$null=Query 'clear_controller'
      for($advance=0;$advance -lt 8;$advance++) {
        $null=Query 'set_controller up';$null=Query 'step 60';$null=Query 'clear_controller'
        $null=Query 'set_controller a';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 36'
        $null=Query ('screenshot '+((Join-Path $root ($warp+'-deep-'+$advance+'.bmp')) -replace '\\','/'))
      }
      $stats=Query 'game presentation_stats'
    }
    if($Menu) {
      $null=Query 'set_controller start';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 90'
      $null=Query 'read_ram 2c 8'
      $null=Query ('screenshot '+((Join-Path $root ($warp+'-menu.bmp')) -replace '\\','/'))
      $null=Query 'step 240'
      $held=Query 'game presentation_stats'
      if(!$Native -and (!$held.menu_active -or $held.failures -or !$held.has_snapshot)) {throw 'Menu lost its frozen scene.'}
      $null=Query ('screenshot '+((Join-Path $root ($warp+'-menu-held.bmp')) -replace '\\','/'))
      $null=Query 'set_controller start';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 90'
    }
    if(!$Native -and (!$stats.supported -or $stats.failures)) {throw "Renderer failure: $($stats|ConvertTo-Json -Compress)"}
    if(!$Native -and $Scale -gt 1 -and $stats.resolution_scale -ne $Scale) {throw 'Incorrect render resolution.'}
    $enhancedMap=$Automap -and (($Scale -gt 1 -and !$Native) -or $Interpolate -or $Transparent)
    if($enhancedMap -and (!$stats.automap_active -or !$stats.automap_lines -or !$stats.automap_draws)) {throw 'Enhanced automap did not draw.'}
    if($Automap -and $Transparent -and ($stats.failures -or !$stats.has_snapshot -or !$stats.camera_passes)) {throw 'Transparent automap world replay failed.'}
    if($Automap -and $Interpolate -and !$stats.automap_interpolated_presentations) {throw 'Automap did not interpolate.'}
    if($Automap) {
      $null=Query 'set_controller y';$null=Query 'step 24';$null=Query 'clear_controller';$null=Query 'step 24'
      $null=Query ('screenshot '+((Join-Path $root ($warp+'-zoom.bmp')) -replace '\\','/'))
      $null=Query 'game cheat_test IDDT';$null=Query 'step 24'
      $null=Query ('screenshot '+((Join-Path $root ($warp+'-monsters.bmp')) -replace '\\','/'))
      $null=Query 'set_controller select';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 36'
      $stats=Query 'game presentation_stats'
      if($stats.automap_active) {throw 'Automap failed to close.'}
      $null=Query ('screenshot '+((Join-Path $root ($warp+'-closed.bmp')) -replace '\\','/'))
    }
    Write-Output "Captured IDCLEV$warp at scale $Scale."
  }
} finally {
  $transcript|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $root 'tcp.json')
  if($client) {$client.Dispose()}
  if($process -and !$process.HasExited) {Stop-Process -Id $process.Id}
  foreach($key in $previous.Keys) {[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}
}



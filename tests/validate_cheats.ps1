param(
  [string]$BinaryDirectory='./build-lag-debug',
  [string]$Rom='./Doom (USA).sfc',
  [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')][string]$RunLabel,
  [int]$DebugPort=4396,
  [switch]$SkipWarps
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
Set-Content -LiteralPath (Join-Path $root 'mods/preloaded/state.toml') -Encoding ascii -Value @'
format_version = 1
[[package]]
id = "doom.cheats"
version = "1.0.0"
[[feature]]
package_id = "doom.cheats"
id = "keyboard-cheats"
enabled = true
'@
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
  $expectedSkill=$s.skill
  $null=Query 'game cheat_test IDDQD';$null=Query 'step 12';$s=Query 'game cheat_stats'
  if(!$s.god -or $s.health -ne 100 -or $s.face -ne 2) {throw 'God mode did not set health and glowing-eye face.'}
  $null=Query 'game cheat_test IDDQD';$null=Query 'step 12';$s=Query 'game cheat_stats'
  if($s.god -or $s.health -ne 100 -or $s.face -eq 2) {throw 'God mode did not disable correctly.'}
  $null=Query 'game cheat_test IDFA';$null=Query 'step 12';$s=Query 'game cheat_stats'
  if($s.armor -ne 200 -or $s.arms -ne 255 -or $s.keys -ne 0 -or ($s.ammo -join ',') -ne '200,50,50,300') {throw 'IDFA inventory mismatch.'}
  $null=Query 'game cheat_test IDKFA';$null=Query 'step 12';$s=Query 'game cheat_stats'
  if($s.keys -ne 63) {throw 'IDKFA did not grant all keys.'}
  $null=Query 'game cheat_test IDDT';$null=Query 'step 12';$s=Query 'game cheat_stats'
  if($s.map -ne 1) {throw 'Full-map stage missing.'}
  $null=Query 'game cheat_test IDDT';$null=Query 'step 12'
  $null=Query 'set_controller select';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 30'
  $mapState=Query 'read_sram 1e6 2'
  if($mapState.hex -eq '00 00') {throw 'Automap did not open.'}
  $null=Query 'set_controller x';$null=Query 'step 180';$null=Query 'clear_controller'
  $s=Query 'game cheat_stats'
  if(!$s.monster_arrows) {throw 'The native automap did not draw living monster arrows.'}
  $null=Query ('screenshot '+((Join-Path $root 'monster-map.bmp') -replace '\\','/'))
  $null=Query 'set_controller select';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 30'
  $null=Query 'game cheat_test IDCLEV11';$null=Query 'step 220'
  $null=Query 'set_controller up';$null=Query 'step 180';$null=Query 'clear_controller'
  $blocked=Query 'game cheat_stats'
  $null=Query 'game cheat_test IDCLEV11';$null=Query 'step 220'
  $null=Query 'game cheat_test IDCLIP';$null=Query 'step 12'
  $null=Query 'set_controller up';$null=Query 'step 180';$null=Query 'clear_controller';$s=Query 'game cheat_stats'
  if(!$s.clip) {throw 'Noclip toggle missing.'}
  if($s.x -eq 1056 -and $s.y -eq -3616) {throw 'Noclip did not move the native player.'}
  if($s.y -le $blocked.y+32) {throw 'Noclip did not cross the wall that blocked normal movement.'}
  Write-Output "Noclip crossed the wall: normal Y=$($blocked.y), noclip Y=$($s.y)."
  $null=Query 'game cheat_test IDCLIP';$null=Query 'step 12'
  $warps=@('11','12','13','14','15','17','18','19','21','23','24','26','28','29','31','32','33','34','36','37','38','39')
  if(!$SkipWarps) {foreach($warp in $warps) {
    $null=Query ('game cheat_test IDCLEV'+$warp);$null=Query 'step 220';$s=Query 'game cheat_stats'
    $expected=([int]::Parse($warp.Substring(0,1))-1)*9+[int]::Parse($warp.Substring(1,1))-1
    if($s.level -ne $expected -or $s.skill -ne $expectedSkill -or $s.health -ne 100 -or $s.armor -ne 0 -or $s.arms -ne 3 -or $s.keys -ne 0 -or ($s.ammo -join ',') -ne '50,0,0,0') {throw "IDCLEV$warp did not reach pistol start: $($s|ConvertTo-Json -Compress)"}
    Write-Output "IDCLEV$warp verified at native level $($s.level), skill $($s.skill)."
  }}
  if(!$SkipWarps) {
    $null=Query 'game cheat_exit_test';$null=Query 'step 220';$s=Query 'game cheat_stats'
    if($s.level -ne 24) {throw 'A normal exit after warping to E3M9 did not return to E3M7.'}
    Write-Output 'Normal exit after IDCLEV39 correctly returns to E3M7.'
  } else {
    $null=Query 'game cheat_test IDCLEV15';$null=Query 'step 220'
    $null=Query 'game cheat_exit_test';$null=Query 'step 220';$s=Query 'game cheat_stats'
    if($s.level -ne 6) {throw 'A normal exit after warping to E1M5 did not skip the missing E1M6.'}
    Write-Output 'Normal exit after IDCLEV15 correctly advances to E1M7.'
  }
  Write-Output 'All gameplay cheat checks passed.'
} finally {
  $transcript|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $root 'tcp.json')
  if($client) {$client.Dispose()}
  if($process -and !$process.HasExited) {Stop-Process -Id $process.Id}
  foreach($key in $previous.Keys) {[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}
}

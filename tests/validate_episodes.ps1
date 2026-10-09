param(
  [string]$BinaryDirectory='./build-lag-debug',
  [string]$Rom='./Doom (USA).sfc',
  [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')][string]$RunLabel,
  [int]$DebugPort=4397,
  [switch]$Disabled
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
$state=@'
format_version = 1
[[package]]
id = "doom.cheats"
version = "1.0.0"
[[feature]]
package_id = "doom.cheats"
id = "keyboard-cheats"
enabled = true
[[package]]
id = "doom.episodes"
version = "1.0.0"
[[feature]]
package_id = "doom.episodes"
id = "unlocked-episodes"
enabled = true
'@
if($Disabled) {$state=$state -replace '(id = "unlocked-episodes"\s+enabled = )true','${1}false'}
Set-Content -LiteralPath (Join-Path $root 'mods/preloaded/state.toml') -Encoding ascii -Value $state
Set-Content -LiteralPath (Join-Path $root 'route.txt') -Encoding ascii -Value @'
wait 400
press start 1
wait 150
press start 1
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
  $null=Query 'run_to_frame 1300'
  do {Start-Sleep -Milliseconds 100;$s=Query 'game cheat_stats'} while($s.frame -lt 1300)
  function Press([string]$button) {
    $null=Query ('set_controller '+$button);$null=Query 'step 12'
    $null=Query 'clear_controller';$null=Query 'step 24'
  }
  function Menu {
    $m=Query 'read_ram 86 5';$bytes=@($m.hex -split ' '|ForEach-Object {[Convert]::ToInt32($_,16)})
    return @{item=$bytes[0]+256*$bytes[1];max=$bytes[4]}
  }
  $m=Menu;Write-Output "Initial menu: item=$($m.item) max=$($m.max)"
  if($m.max -ne 5) {Press 'start';$null=Query 'step 120';$m=Menu}
  if($m.max -eq 4) {Press 'down';Press 'a';$null=Query 'step 60';$m=Menu}
  if($m.max -ne 5) {throw 'Did not reach skill selection.'}
  function SelectItem([int]$item) {
    $m=Menu
    for($attempt=0; $m.item -ne $item; $attempt++) {
      if($attempt -ge 10) {throw "Menu selection stalled."}
      Press $(if($m.item -gt $item) {'up'} else {'down'})
      $m=Menu
    }
  }
  foreach($skill in 0..4) {
    SelectItem $skill;Press 'a';$null=Query 'step 60';$m=Menu
    $expected=if(!$Disabled) {3} elseif($skill -lt 2) {1} elseif($skill -eq 2) {2} else {3}
    if($m.max -ne $expected) {throw "Skill ${skill}: expected $expected episodes, found $($m.max)."}
    SelectItem ($expected-1)
    $null=Query ('screenshot '+((Join-Path $root ('skill-'+$skill+'-episodes.bmp')) -replace '\\','/'))
    Write-Output "Skill ${skill}: $($m.max) episodes selectable."
    Press 'b';$null=Query 'step 36'
    if((Menu).max -ne 5) {throw 'Could not return to skill menu.'}
  }
  if(!$Disabled) {
    SelectItem 0;Press 'a';$null=Query 'step 60';SelectItem 2;Press 'a';$null=Query 'step 360'
    $s=Query 'game cheat_stats'
    if(!$s.ready -or $s.level -ne 18 -or $s.skill -ne 0) {throw 'Episode 3 did not start on I am Too Young to Die.'}
    foreach($transition in @(@{code='18';next=9},@{code='28';next=18})) {
      $null=Query ('game cheat_test IDCLEV'+$transition.code);$null=Query 'step 220'
      $null=Query 'game cheat_exit_test';$null=Query 'step 220'
      $s=Query 'game cheat_stats'
      for($i=0;$i -lt 20 -and !$s.ready;$i++) {Press 'a';$null=Query 'step 120';$s=Query 'game cheat_stats'}
      if(!$s.ready -or $s.level -ne $transition.next -or $s.skill -ne 0) {throw "Episode transition failed: $($s|ConvertTo-Json -Compress)"}
      Write-Output "Boss exit IDCLEV$($transition.code) reached native level $($s.level), skill $($s.skill)."
    }
  }
  Write-Output 'Episode selection and progression checks passed.'
} finally {
  $transcript|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $root 'tcp.json')
  if($client) {$client.Dispose()}
  if($process -and !$process.HasExited) {Stop-Process -Id $process.Id}
  foreach($key in $previous.Keys) {[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}
}





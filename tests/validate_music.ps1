param(
  [string]$BinaryDirectory='./build-lag-debug',
  [string]$Rom='./Doom (USA).sfc',
  [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')][string]$RunLabel,
  [int]$DebugPort=4396,
  [switch]$Enhancements,
  [switch]$Desktop,
  [switch]$Quick
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
foreach($setting in @('Autosave','DisableFrameDelay','SourceP1','SourceP2','VSync')) {
  $config=$config -replace ('(?m)^'+$setting+'\s*=\s*[^\r\n]*'),($setting+' = 0')
}
$config=$config -replace '(?m)^OutputMethod\s*=\s*[^\r\n]*','OutputMethod = SDL-Software'
$config=$config -replace '(?m)^DisableFrameDelay\s*=\s*[^\r\n]*',('DisableFrameDelay = '+[int](!$Desktop))
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
if($Enhancements) {
  Add-Content -LiteralPath (Join-Path $root 'mods/preloaded/state.toml') -Encoding ascii -Value @'
[[package]]
id = "doom.presentation"
version = "1.0.0"
[[feature]]
package_id = "doom.presentation"
id = "widescreen"
enabled = true
[feature.values]
aspect = "16:9"
[[feature]]
package_id = "doom.presentation"
id = "render-resolution"
enabled = true
[feature.values]
scale = "4"
[[feature]]
package_id = "doom.presentation"
id = "presentation-fps"
enabled = true
[[feature]]
package_id = "doom.presentation"
id = "modern-controls"
enabled = true
'@
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
$envs=@{SDL_VIDEODRIVER=$(if($Desktop){$null}else{'dummy'});SDL_AUDIODRIVER=$(if($Desktop){$null}else{'dummy'});SNESRECOMP_DEBUG_PORT=[string]$DebugPort;
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
  $initial=Query 'game cheat_stats'
  $songs=@{
    '11'=0x0402;'12'=0x0503;'13'=0x0604;'14'=0x0705;'15'=0x0806;
    '17'=0x0a08;'18'=0x0b09;'19'=0x0c0a;'21'=0x0c0a;'23'=0x0d0b;
    '24'=0x0e0c;'26'=0x0f0d;'28'=0x110f;'29'=0x1210;'31'=0x1210;
    '32'=0x1311;'33'=0x0907;'34'=0x0b09;'36'=0x0907;'37'=0x100e;
    '38'=0x1412;'39'=0x0c0a
  }
  $codes=if($Quick){@('11','12','11')}else{@('11','12','13','14','15','17','18','19','21','23','24','26','28','29','31','32','33','34','36','37','38','39','11','11')}
  foreach($code in $codes) {
    $null=Query ('game cheat_test IDMUS'+$code)
    if($code -eq '12' -and $Enhancements) {
      for($loaderWait=0;$loaderWait -lt 60;$loaderWait++) {
        $null=Query 'step 1'
        Start-Sleep -Milliseconds 25
        $firstState=Query 'game music_stats'
        if($firstState.loading){break}
      }
      Start-Sleep -Milliseconds 150
      if(!$firstState.loading){throw 'Music picture check did not enter the loader.'}
      $first=Join-Path $root 'music-upload-first.bmp'
      $second=Join-Path $root 'music-upload-second.bmp'
      $null=Query ('screenshot '+($first -replace '\\','/'))
      $null=Query 'step 1'
      Start-Sleep -Milliseconds 150
      $secondState=Query 'game music_stats'
      $null=Query ('screenshot '+($second -replace '\\','/'))
      if($secondState.loading -and
         (Get-FileHash -LiteralPath $first).Hash -ne (Get-FileHash -LiteralPath $second).Hash) {
        throw 'Music upload did not hold a stable picture.'
      }
    }
    $null=Query 'step 180'
    for($wait=0;$wait -lt 40;$wait++) {
      $current=Query 'game cheat_stats'
      if($current.ready){break}
      $null=Query 'step 30'
    }
    if(!$current.ready){
      Query 'read_ram 2b 8'|ConvertTo-Json
      Query 'interp_edges n=12'|ConvertTo-Json -Depth 5
      throw ('Gameplay did not resume after IDMUS'+$code)
    }
    $song=Query 'read_ram 6bd 4'
    $bytes=@($song.hex -split ' ' | ForEach-Object {[Convert]::ToInt32($_,16)})
    if(($bytes[2]+256*$bytes[3]) -ne $songs[$code] -or
       ($bytes[0]+256*$bytes[1]) -ne ($songs[$code] -shr 8)) {
      throw ('Incorrect native song/module for IDMUS'+$code)
    }
    $flags=Query 'read_ram 2b 8'
    $current=Query 'game cheat_stats'
    Write-Output ($code+' song='+$song.hex+' flags='+$flags.hex)
    if($current.level -ne $initial.level -or $current.health -ne $initial.health -or
       $current.skill -ne $initial.skill -or $current.arms -ne $initial.arms -or
       $current.x -ne $initial.x -or $current.y -ne $initial.y -or
       ($current.ammo -join ',') -ne ($initial.ammo -join ',')){
      Query 'read_ram 20c5 80'|ConvertTo-Json
      Query 'interp_edges n=12'|ConvertTo-Json -Depth 5
      throw 'Music cheat changed level/player.'
    }
  }
  $null=Query ('screenshot '+((Join-Path $root 'music.bmp') -replace '\\','/'))
  Write-Output ('Music checks passed with audio enabled: '+($codes -join ', '))
} finally {
  $transcript|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $root 'tcp.json')
  if($client) {$client.Dispose()}
  if($process -and !$process.HasExited) {Stop-Process -Id $process.Id}
  foreach($key in $previous.Keys) {[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}
}

# Capture nearby E1M2 Nightmare sprites crossing the widescreen join.
param(
 [string]$BinaryDirectory='./build-lag-debug',
 [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')][string]$RunLabel,
 [int]$DebugPort=4399
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$source=(Resolve-Path -LiteralPath $BinaryDirectory).Path
$root=Join-Path $repo ('build-lag-evidence/'+$RunLabel)
if(Test-Path -LiteralPath $root){throw 'Run already exists'}
New-Item -ItemType Directory -Path (Join-Path $root 'mods/preloaded') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'DoomSNESRecomp.exe') -Destination $root
Copy-Item -LiteralPath (Join-Path $source 'mods/preloaded/packages') -Destination (Join-Path $root 'mods/preloaded') -Recurse
$on='true'
Set-Content -LiteralPath (Join-Path $root 'mods/preloaded/state.toml') -Encoding ascii -Value @"
format_version = 1
[[package]]
id = "doom.cheats"
version = "1.0.0"
[[feature]]
package_id = "doom.cheats"
id = "keyboard-cheats"
enabled = true
[[package]]
id = "doom.presentation"
version = "1.0.0"
[[feature]]
package_id = "doom.presentation"
id = "widescreen"
enabled = $on
[feature.values]
aspect = "16:9"
[[feature]]
package_id = "doom.presentation"
id = "render-resolution"
enabled = $on
[feature.values]
scale = "4"
[[feature]]
package_id = "doom.presentation"
id = "modern-controls"
enabled = $on
[[feature]]
package_id = "doom.presentation"
id = "presentation-fps"
enabled = $on
"@
$config=Get-Content -LiteralPath (Join-Path $repo 'build/config.ini') -Raw
foreach($setting in @('Autosave','DisableFrameDelay','SourceP1','SourceP2','VSync')){
 $config=$config -replace ('(?m)^'+$setting+'\s*=\s*[^\r\n]*'),($setting+' = 0')
}
$config=$config -replace '(?m)^OutputMethod\s*=\s*[^\r\n]*','OutputMethod = SDL-Software'
Set-Content -LiteralPath (Join-Path $root 'config.ini') -Value $config -Encoding ascii

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
$envs=@{SDL_VIDEODRIVER='dummy';SDL_AUDIODRIVER='dummy';SNESRECOMP_DEBUG_PORT=[string]$DebugPort;SNESRECOMP_RUN_FRAMES='18000';SNESRECOMP_RUNAHEAD='0';SNESRECOMP_REWIND='0'}
$previous=@{};$process=$null;$client=$null;$transcript=[Collections.Generic.List[object]]::new()
try {
 foreach($key in $envs.Keys){$previous[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$envs[$key],'Process')}
 $arguments=@('--no-launcher','--script',('"'+(Join-Path $root 'route.txt')+'"'),'--rom',('"'+(Join-Path $repo 'Doom (USA).sfc')+'"'))
 $process=Start-Process -FilePath (Join-Path $root 'DoomSNESRecomp.exe') -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'stdout.log') -RedirectStandardError (Join-Path $root 'stderr.log')
 $deadline=[DateTime]::UtcNow.AddSeconds(30)
 do{$client=[Net.Sockets.TcpClient]::new();try{$client.Connect('127.0.0.1',$DebugPort)}catch{$client.Dispose();$client=$null;Start-Sleep -Milliseconds 100}}while(!$client -and [DateTime]::UtcNow -lt $deadline)
 if(!$client){throw 'TCP startup timed out'}
 $stream=$client.GetStream();$stream.ReadTimeout=30000
 $reader=[IO.StreamReader]::new($stream);$writer=[IO.StreamWriter]::new($stream);$writer.AutoFlush=$true
 function Query([string]$command){$writer.WriteLine($command);$line=$reader.ReadLine();if(!$line){throw "TCP closed: $command"};$result=$line|ConvertFrom-Json;$transcript.Add(@{command=$command;result=$result});if($result.error){throw "$command : $($result.error)"};if($result.timeout -and $result.stepped){$target=$result.frame_before+$result.stepped; do{Start-Sleep -Milliseconds 100;$poll=Query 'game presentation_stats'}while($poll.frame -lt $target)};return $result}
 $null=Query 'run_to_frame 1400'
 do{Start-Sleep -Milliseconds 100;$s=Query 'game cheat_stats'}while($s.frame -lt 1400)
 Write-Output ($s|ConvertTo-Json -Compress)
 function Tap([string]$key){$null=Query ('set_controller '+$key);$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 60'}
 Tap 'start';Tap 'down';Tap 'a';Tap 'down';Tap 'down';Tap 'a';Tap 'a'
 $null=Query 'step 350';$s=Query 'game cheat_stats';Write-Output ($s|ConvertTo-Json -Compress);if($s.skill -ne 4){throw 'Nightmare setup failed'}
 $null=Query 'game cheat_test IDCLEV12';$null=Query 'step 350';$null=Query 'game cheat_test IDDQD';$null=Query 'step 12'
 $s=Query 'game cheat_stats'
 if($s.skill -ne 4 -or $s.level -ne 1 -or !$s.god){throw 'E1M2 Nightmare fixture setup failed'}
 $null=Query 'step 700'
 foreach($turn in @(0,5,5,5,5,5,5,5,5,5,5,5)) {
  if($turn){$null=Query 'set_controller left';$null=Query ('step '+$turn);$null=Query 'clear_controller'}
  $tag='turn-'+$transcript.Count
  $null=Query ('screenshot '+((Join-Path $root ($tag+'.bmp')) -replace '\\','/'))
  $s=Query 'game presentation_stats'
  if($s.failures -or !$s.supported -or !$s.has_snapshot -or $s.resolution_scale -ne 4){throw 'Enhanced renderer unavailable or replay failed'}
 }
 Write-Output ($s|ConvertTo-Json -Compress)
} finally {
 $transcript|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $root 'tcp.json')
 if($client){$client.Dispose()}
 if($process -and !$process.HasExited){Stop-Process -Id $process.Id}
 foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}
}










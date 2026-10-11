# Verify pixels, not just the native pause flag. Keyboard events must also
# traverse host hotkeys: direct cheat_test alone missed the R renderer toggle.
param(
 [string]$BinaryDirectory='./build-lag-debug',
 [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')][string]$RunLabel,
 [int]$DebugPort=4399,[switch]$Stock,[switch]$Desktop,[switch]$Legacy,[switch]$Paused
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$source=(Resolve-Path -LiteralPath $BinaryDirectory).Path
$root=Join-Path $repo ('build-lag-evidence/'+$RunLabel)
if(Test-Path -LiteralPath $root){throw 'Run already exists'}
New-Item -ItemType Directory -Path (Join-Path $root 'mods/preloaded') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'DoomSNESRecomp.exe') -Destination $root
Copy-Item -LiteralPath (Join-Path $source 'mods/preloaded/packages') -Destination (Join-Path $root 'mods/preloaded') -Recurse
$on=if($Stock){'false'}else{'true'}
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
if($Legacy){$config=$config -replace '(?m)^NewRenderer\s*=\s*[^\r\n]*','NewRenderer = 0';Set-Content -LiteralPath (Join-Path $root 'config.ini') -Value $config -Encoding ascii}
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
$envs=@{SDL_VIDEODRIVER=$(if($Desktop){$null}else{'dummy'});SDL_AUDIODRIVER=$(if($Desktop){$null}else{'dummy'});SNESRECOMP_DEBUG_PORT=[string]$DebugPort;SNESRECOMP_RUN_FRAMES='18000';SNESRECOMP_RUNAHEAD='0';SNESRECOMP_REWIND='0'}
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
 function Query([string]$command){$writer.WriteLine($command);$line=$reader.ReadLine();if(!$line){throw "TCP closed: $command"};$result=$line|ConvertFrom-Json;$transcript.Add(@{command=$command;result=$result});if($result.error){throw "$command : $($result.error)"};return $result}
 $null=Query 'run_to_frame 1400'
 do{Start-Sleep -Milliseconds 100;$before=Query 'game cheat_stats'}while($before.frame -lt 1400)
 if($Paused) {
  $null=Query 'set_controller start';$null=Query 'step 12'
  $null=Query 'clear_controller';$null=Query 'step 60'
  $pause=Query 'game presentation_stats'
  if(!$pause.menu_active){throw 'Pause menu did not open before snapshot'}
 }
 $beforePpu=Query 'get_ppu_state'
 $null=Query ('screenshot '+((Join-Path $root 'before-restart.bmp') -replace '\\','/'))
 $null=Query 'savestate 1';$null=Query 'step 12'
 $path=Join-Path $root 'saves/save1.sav'
 $deadline=[DateTime]::UtcNow.AddSeconds(10)
 while(!(Test-Path -LiteralPath $path) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 100}
 if(!(Test-Path -LiteralPath $path)){throw 'Snapshot missing'}
 Copy-Item -LiteralPath $path -Destination (Join-Path $root 'resume-copy.sav')
 $client.Dispose();$client=$null;Stop-Process -Id $process.Id;$process.WaitForExit()
 # Change presentation mods before restarting, as the launcher does.
 $mods=Join-Path $root 'mods/preloaded/state.toml'
 $state=Get-Content -LiteralPath $mods -Raw
 $state=$state.Replace('scale = '+[char]34+'4'+[char]34,'scale = '+[char]34+'2'+[char]34)
 Set-Content -LiteralPath $mods -Value $state -Encoding ascii
 # Re-enter a fresh process through the same path used by Ctrl+L mod changes.
 $env:SNESRECOMP_RUN_FRAMES='300'
 $arguments=@('--no-launcher','--resume-state',('"'+(Join-Path $root 'resume-copy.sav')+'"'),'--rom',('"'+(Join-Path $repo 'Doom (USA).sfc')+'"'))
 $process=Start-Process -FilePath (Join-Path $root 'DoomSNESRecomp.exe') -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'resumed-stdout.log') -RedirectStandardError (Join-Path $root 'resumed-stderr.log')
 $deadline=[DateTime]::UtcNow.AddSeconds(30)
 do{$client=[Net.Sockets.TcpClient]::new();try{$client.Connect('127.0.0.1',$DebugPort)}catch{$client.Dispose();$client=$null;Start-Sleep -Milliseconds 100}}while(!$client -and [DateTime]::UtcNow -lt $deadline)
 if(!$client){throw 'Resumed TCP startup timed out'}
 $stream=$client.GetStream();$stream.ReadTimeout=30000;$reader=[IO.StreamReader]::new($stream);$writer=[IO.StreamWriter]::new($stream);$writer.AutoFlush=$true

 do{Start-Sleep -Milliseconds 100;$after=Query 'game cheat_stats'}while($after.frame -lt 100)
 $afterPpu=Query 'get_ppu_state'
 if($beforePpu.vramIncrementOnHigh -ne $afterPpu.vramIncrementOnHigh -or
    $beforePpu.vramIncrement -ne $afterPpu.vramIncrement -or
    $beforePpu.vramRemapMode -ne $afterPpu.vramRemapMode){throw 'Graphics upload port changed on process restart'}
 $null=Query ('screenshot '+((Join-Path $root 'after-restart.bmp') -replace '\\','/'))
 if($Paused) {
  $pause=Query 'game presentation_stats'
  if(!$pause.menu_active){throw 'Paused state did not survive process restart'}
  if(!$Stock -and (!$pause.has_snapshot -or !$pause.resolution_segments -or $pause.failures -or $pause.resolution_scale -ne 2)) {
   throw 'Enhanced paused view was not rebuilt after restart'
  }
  $null=Query ('screenshot '+((Join-Path $root 'resumed-menu.bmp') -replace '\\','/'))
  if(!$Stock) {
   Add-Type -AssemblyName System.Drawing
   $a=[Drawing.Bitmap]::new((Join-Path $root 'before-restart.bmp'))
   $b=[Drawing.Bitmap]::new((Join-Path $root 'resumed-menu.bmp'))
   try {
    $sa=$a.Height/224;$sb=$b.Height/224;$different=0
    # Animated face pixels are checked visually; the fixed status artwork
    # and numeric counters must survive the paused mod restart exactly.
    for($y=176;$y -lt 224;$y++){for($x=0;$x -lt 256;$x++){
     if($x -ge 112 -and $x -lt 152){continue}
     $pa=$a.GetPixel([int](($a.Width-256*$sa)/2+$x*$sa),[int]($y*$sa)).ToArgb()
     $pb=$b.GetPixel([int](($b.Width-256*$sb)/2+$x*$sb),[int]($y*$sb)).ToArgb()
     if($pa -ne $pb){$different++}
    }}
    if($different){throw "Paused HUD changed after restart: $different native pixels"}
   } finally {$a.Dispose();$b.Dispose()}
  }
  $null=Query 'set_controller start';$null=Query 'step 12'
  $null=Query 'clear_controller';$null=Query 'step 60'
  $after=Query 'game cheat_stats'
 }
 if(!$after.ready -or $after.x -ne $before.x -or $after.y -ne $before.y -or $after.level -ne $before.level){throw 'Gameplay state not restored'}
 if(Select-String -LiteralPath (Join-Path $root 'resumed-stderr.log') -Pattern 'architectural BRK'){throw 'Guest CPU corrupted on resume'}
 Write-Output 'Fresh-process gameplay resume preserved coordinates and level, without BRK.'
} finally {
 $transcript|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $root 'tcp.json')
 if($client){$client.Dispose()}
 if($process -and !$process.HasExited){Stop-Process -Id $process.Id}
 foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}
}




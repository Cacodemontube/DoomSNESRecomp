# Verify pixels, not just the native pause flag. Keyboard events must also
# traverse host hotkeys: direct cheat_test alone missed the R renderer toggle.
param(
 [string]$BinaryDirectory='./build-lag-debug',
 [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')][string]$RunLabel,
 [int]$DebugPort=4399,[switch]$Stock,[switch]$Desktop,[switch]$Legacy
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
 do{Start-Sleep -Milliseconds 100;$stats=Query 'game presentation_stats'}while($stats.frame -lt 1400)
 Add-Type -AssemblyName System.Drawing
 function CaptureMenu([string]$name) {
   Start-Sleep -Milliseconds 150
   $path=Join-Path $root ($name+'.bmp')
   $null=Query ('screenshot '+($path -replace '\\','/'))
   $bitmap=[Drawing.Bitmap]::new($path)
   $red=0
   for($y=50;$y -lt 125;$y++){for($x=80;$x -lt 215;$x++){
     $p=$bitmap.GetPixel([int](($bitmap.Width-256*($bitmap.Height/224))/2+$x*($bitmap.Height/224)),[int]($y*($bitmap.Height/224)))
     if($p.R -gt 70 -and $p.R -gt 2*$p.G -and $p.R -gt 2*$p.B){$red++}
   }}
   $bitmap.Dispose()
   $s=Query 'game presentation_stats'
   Write-Output ($name+' red='+$red+' menu='+$s.menu_active)
   if($red -lt 100){$null=Query 'read_ram 2b 8';$null=Query 'raster_journal';throw ('Menu text missing: '+$name)}
 }
 function PauseGame {
   $null=Query 'set_controller start';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 60'
 }
 PauseGame
 CaptureMenu 'initial'
 $null=Query 'set_controller start';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 120'
 for($i=0;$i -lt 12;$i++){
   # Grant via the deterministic hook too: headless SDL has no keyboard
   # focus. Inject the spelling separately to check the host event route.
   $null=Query 'game cheat_test IDCHOPPERS';$null=Query 'step 12';$null=Query 'game cheat_keyboard_test IDCHOPPERS'
   $delay=@(1,2,5,12,30,120)[$i%6]
   $null=Query ('step '+$delay)
   PauseGame
   CaptureMenu ('choppers-'+$i)
   $null=Query 'step 120'
   CaptureMenu ('held-'+$i)
   $null=Query 'set_controller start';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 60'
   $s=Query 'game cheat_stats'
   if(!$s.ready){throw 'Gameplay did not resume'}
   if(!($s.arms -band 8)){throw 'IDCHOPPERS did not grant the chainsaw'}
 }
 PauseGame
 $null=Query 'set_controller down';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 60'
 CaptureMenu 'menu-down'
 $null=Query 'set_controller a';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 120'
 CaptureMenu 'skill'
 $null=Query 'set_controller a';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 120'
 CaptureMenu 'episode'
 Write-Output 'Startup, twelve chainsaw/pause cycles, held menus and submenu pixels passed.'
} finally {
 $transcript|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $root 'tcp.json')
 if($client){$client.Dispose()}
 if($process -and !$process.HasExited){Stop-Process -Id $process.Id}
 foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}
}

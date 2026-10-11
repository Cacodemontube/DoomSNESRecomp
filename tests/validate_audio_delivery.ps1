# Observe uninterrupted E1M1 audio delivery and compare DSP arithmetic.
# Requires the trace build; -Desktop uses the actual output device.
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
 # Keep running beyond both observation points: stopping the guest while the
 # audio device drains its queue would manufacture a test-only underflow.
 $null=Query 'run_to_frame 2600'
 do{Start-Sleep -Milliseconds 50;$stats=Query 'game presentation_stats'}while($stats.frame -lt 1400)
 $initial=Query 'audio_stats 0'
 do{Start-Sleep -Milliseconds 50;$stats=Query 'game presentation_stats'}while($stats.frame -lt 2000)
 $final=Query 'audio_stats 10'
 $reference=Query 'audio_shadow_div'
 @{initial=$initial;final=$final;reference=$reference}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $root 'audio-summary.json')
 $null=Query ('audio_wav '+((Join-Path $root 'native-music.wav') -replace '\\','/')+' '+[Math]::Max(0,$final.produced-320400)+' 320400')
 if($final.produced_audio){throw 'The audio consumer advanced guest emulation'}
 if($final.output_underflows -ne $initial.output_underflows -or $final.dropped_audible -ne $initial.dropped_audible){throw 'E1M1 audio delivery starved or dropped audible samples'}
 if($reference.faithful_brr.first.valid){throw 'BRR sample decoding diverged from the in-process reference'}
 Write-Output ('E1M1: fields '+$stats.frame+', no new output underflows or dropped audible samples; BRR decoding matches reference.')
 $reference|ConvertTo-Json -Depth 6
} finally {
 $transcript|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $root 'tcp.json')
 if($client){$client.Dispose()}
 if($process -and !$process.HasExited){Stop-Process -Id $process.Id}
 foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}
}

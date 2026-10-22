param([string]$BinaryDirectory='build-lag-debug',[string]$RunLabel='graphics-fixed',[int]$Port=8731,[switch]$Rotations,[switch]$Crosshair,[ValidateSet('18','31')][string]$Warp='31',[switch]$WalkOut,[switch]$Native,[switch]$Infighting)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$source=(Resolve-Path (Join-Path $repo $BinaryDirectory)).Path
$cache=Join-Path $source 'CMakeCache.txt'
if((Test-Path -LiteralPath $cache) -and (Select-String -LiteralPath $cache -SimpleMatch 'SNESRECOMP_ENABLE_TRACE:BOOL=OFF')){throw 'TCP capture requires a trace build, for example build-lag-debug'}
$root=Join-Path $repo "build-evidence/graphics-fix/$RunLabel"
if(Test-Path -LiteralPath $root){throw 'Evidence run already exists'}
New-Item -ItemType Directory -Path $root | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'DoomSNESRecomp.exe') -Destination $root
foreach($dir in @('assets','mods')){Copy-Item -LiteralPath (Join-Path $source $dir) -Destination $root -Recurse}
$config=Get-Content (Join-Path $repo 'dist/DoomSNESRecomp-Jitter-Fix-Local-20261015-Windows-x64/config.ini') -Raw
$config=$config.Replace('Renderer = auto','Renderer = software').Replace('OutputMethod = SDL','OutputMethod = SDL-Software').Replace('EnableAudio = 1','EnableAudio = 0').Replace('DisableFrameDelay = 0','DisableFrameDelay = 1')
Set-Content (Join-Path $root 'config.ini') $config -Encoding ascii
$state=@('format_version = 1')
foreach($pair in @(@('doom.cheats','keyboard-cheats'),@('doom.presentation','widescreen'))){
    $state+=@('[[package]]',('id = "'+$pair[0]+'"'),'version = "1.0.0"','[[feature]]',('package_id = "'+$pair[0]+'"'),('id = "'+$pair[1]+'"'),'enabled = true')
    if($pair[1] -eq 'widescreen'){$state+=@('[feature.values]','aspect = "16:9"')}
}
if($Rotations){$state+=@('[[package]]','id = "doom.rotations"','version = "1.0.0"','[[feature]]','package_id = "doom.rotations"','id = "pc-rotations"','enabled = true','[[resource]]','package_id = "doom.rotations"','feature_id = "pc-rotations"','id = "doom-wad"',('path = "'+(($repo+'/DOOM.WAD') -replace '\\','/')+'"'))}
if($Crosshair){$state+=@('[[package]]','id = "doom.crosshair"','version = "1.0.0"','[[feature]]','package_id = "doom.crosshair"','id = "autoaim-crosshair"','enabled = true')}
if($Infighting){$state+=@('[[package]]','id = "doom.infighting"','version = "1.0.0"','[[feature]]','package_id = "doom.infighting"','id = "monster-infighting"','enabled = true')}
if($Native){
    $text=($state -join "`n").Replace('id = "widescreen"'+"`n"+'enabled = true','id = "widescreen"'+"`n"+'enabled = false')
    $state=@($text,'[[feature]]','package_id = "doom.presentation"','id = "presentation-fps"','enabled = true','[feature.values]','fps = "120"')
}
Set-Content (Join-Path $root 'mods/preloaded/state.toml') $state -Encoding ascii
Set-Content (Join-Path $root 'route.txt') @('wait 400','press start 1','wait 150','press start 1','wait 150','press a 1','wait 150','press a 1','wait 16000','quit') -Encoding ascii
$previous=@{};$process=$null;$client=$null;$transcript=[Collections.Generic.List[object]]::new()
try{
    $envs=@{SDL_VIDEODRIVER='dummy';SDL_AUDIODRIVER='dummy';SNESRECOMP_DEBUG_PORT="$Port";SNESRECOMP_START_PAUSED='1';SNESRECOMP_RUN_FRAMES='16000';SNESRECOMP_REWIND='0';DOOM_RENDER_STATS='1'}
    foreach($key in $envs.Keys){$previous[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$envs[$key],'Process')}
    $arguments=@('--no-launcher','--script',('"'+(Join-Path $root 'route.txt')+'"'),'--rom',('"'+(Join-Path $repo 'Doom (USA).sfc')+'"'))
    $process=Start-Process -FilePath (Join-Path $root 'DoomSNESRecomp.exe') -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'stdout.log') -RedirectStandardError (Join-Path $root 'stderr.log')
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    do{
        $client=[Net.Sockets.TcpClient]::new()
        try{$client.Connect('127.0.0.1',$Port)}catch{$client.Dispose();$client=$null;Start-Sleep -Milliseconds 100}
        if($process.HasExited){throw 'Game exited before debug startup'}
    }while(!$client -and [DateTime]::UtcNow -lt $deadline)
    if(!$client){throw 'Debug startup timed out'}
    $stream=$client.GetStream();$stream.ReadTimeout=30000
    $reader=[IO.StreamReader]::new($stream);$writer=[IO.StreamWriter]::new($stream);$writer.AutoFlush=$true
    function Query([string]$cmd){$writer.WriteLine($cmd);$line=$reader.ReadLine();if(!$line){throw "Debug connection closed: $cmd"};$result=$line|ConvertFrom-Json;$transcript.Add(@{command=$cmd;result=$result});if($result.error){throw "$cmd : $($result.error)"};return $result}
    $null=Query 'run_to_frame 1400'
    do{Start-Sleep -Milliseconds 100;$stats=Query 'game cheat_stats'}while($stats.frame -lt 1400)
    if(!$stats.ready){throw 'Gameplay not reached'}
    $null=Query ('screenshot '+((Join-Path $root 'e1m1.bmp') -replace '\\','/'))
    $null=Query ('game cheat_test IDCLEV'+$Warp);$null=Query 'step 900'
    $stats=Query 'game cheat_stats'
    $expected=if($Warp -eq '18'){7}else{18}
    if($stats.level -ne $expected){throw "Map warp failed: $($stats.level)"}
    $null=Query 'game cheat_test IDDQD';$null=Query 'step 12'
    if($Warp -eq '31' -or $WalkOut){
    $null=Query 'set_controller a';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 90'
    $null=Query 'set_controller up';$null=Query 'step 120';$null=Query 'clear_controller';$null=Query 'step 12'
    $null=Query 'set_controller a';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 90'
    $null=Query 'set_controller up';$null=Query 'step 160';$null=Query 'clear_controller';$null=Query 'step 12'
    }
    $scene=if($Warp -eq '18'){'e1m8'}else{'e3m1'}
    for($i=0;$i -lt 8;$i++){
        $null=Query ('screenshot '+((Join-Path $root "$scene-$i.bmp") -replace '\\','/'))
        $null=Query 'set_controller right';$null=Query 'step 12';$null=Query 'clear_controller';$null=Query 'step 6'
    }
    $null=Query 'game presentation_stats'
    Write-Output "Captured native E1M1 and eight $scene views: $root"
}finally{
    $transcript|ConvertTo-Json -Depth 6|Set-Content (Join-Path $root 'tcp.json')
    if($client){$client.Dispose()}
    if($process -and !$process.HasExited){Stop-Process -Id $process.Id}
    foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}
}

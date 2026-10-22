param([string]$RunLabel='native-on',[int]$Port=8742,[switch]$Disabled,[string]$Warp='31',[switch]$Explore,[switch]$StateOnly)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$source=Join-Path $repo 'build-lag-debug'
$root=Join-Path $repo "build-evidence/infighting/$RunLabel"
if(Test-Path -LiteralPath $root){throw 'Evidence directory already exists'}
New-Item -ItemType Directory -Path $root | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'DoomSNESRecomp.exe') -Destination $root
foreach($dir in @('assets','mods')){Copy-Item -LiteralPath (Join-Path $source $dir) -Destination $root -Recurse}
$config=Get-Content (Join-Path $repo 'dist/DoomSNESRecomp-Jitter-Fix-Local-20261015-Windows-x64/config.ini') -Raw
$config=$config.Replace('Renderer = auto','Renderer = software').Replace('OutputMethod = SDL','OutputMethod = SDL-Software').Replace('EnableAudio = 1','EnableAudio = 0').Replace('DisableFrameDelay = 0','DisableFrameDelay = 1')
Set-Content (Join-Path $root 'config.ini') $config -Encoding ascii
$state=@('format_version = 1')
$features=,@('doom.cheats','keyboard-cheats')
if(!$Disabled){$features+=,@('doom.infighting','monster-infighting')}
foreach($pair in $features){$state+=@('[[package]]',('id = "'+$pair[0]+'"'),'version = "1.0.0"','[[feature]]',('package_id = "'+$pair[0]+'"'),('id = "'+$pair[1]+'"'),'enabled = true')}
Set-Content (Join-Path $root 'mods/preloaded/state.toml') $state -Encoding ascii
Set-Content (Join-Path $root 'route.txt') @('wait 400','press start 1','wait 150','press start 1','wait 150','press a 1','wait 150','press a 1','wait 30000','quit') -Encoding ascii
$previous=@{};$process=$null;$client=$null;$transcript=[Collections.Generic.List[object]]::new()
try{
    $envs=@{SDL_VIDEODRIVER='dummy';SDL_AUDIODRIVER='dummy';SNESRECOMP_DEBUG_PORT="$Port";SNESRECOMP_START_PAUSED='1';SNESRECOMP_RUN_FRAMES='30000';SNESRECOMP_REWIND='0'}
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
    function Query([string]$cmd){
        $writer.WriteLine($cmd);$line=$reader.ReadLine();if(!$line){throw "Connection closed: $cmd"}
        $result=$line|ConvertFrom-Json;$transcript.Add(@{command=$cmd;result=$result});if($result.error){throw "$cmd : $($result.error)"};return $result
    }
    function Advance([int]$count){
        $current=Query 'game cheat_stats';$target=[int]$current.frame+$count
        $null=Query "run_to_frame $target";$limit=[DateTime]::UtcNow.AddSeconds(180)
        do{Start-Sleep -Milliseconds 100;$current=Query 'game cheat_stats';if([DateTime]::UtcNow -gt $limit){throw 'Frame advance timed out'}}while($current.frame -lt $target)
    }
    Advance 1400
    $stats=Query 'game infighting_stats'
    if([bool]$stats.ready -eq [bool]$Disabled){throw 'Infighting activation mismatch'}
    if(!$StateOnly){
    $current=Query 'game cheat_stats'
    if(!$current.god){$null=Query 'game cheat_test IDDQD';Advance 30}
    $null=Query ('game cheat_test IDCLEV'+$Warp);Advance 900
    $current=Query 'game cheat_stats'
    if(!$current.god){$null=Query 'game cheat_test IDDQD';Advance 30}
    foreach($count in @(120,160)){
        $null=Query 'set_controller a';Advance 12;$null=Query 'clear_controller';Advance 90
        $null=Query 'set_controller up';Advance $count;$null=Query 'clear_controller';Advance 12
    }
    for($i=0;$i -lt 4;$i++){
        if($Explore){
            $null=Query 'set_controller right';Advance 45;$null=Query 'clear_controller'
            $null=Query 'set_controller a';Advance 12;$null=Query 'clear_controller';Advance 45
            $null=Query 'set_controller up';Advance 90;$null=Query 'clear_controller'
        }
        Advance 600
        $null=Query ('screenshot '+((Join-Path $root "fight-$i.bmp") -replace '\\','/'))
        $stats=Query 'game infighting_stats';Write-Output ($stats|ConvertTo-Json -Compress)
    }
    if(!$Disabled -and !$stats.hits){throw 'No native attack activity observed'}
    if($Disabled -and ($stats.births -or $stats.hits -or $stats.retaliations)){throw 'Disabled mod changed simulation'}
    }
    $null=Query 'savestate 8';Advance 10
    $null=Query 'loadstate 8';Advance 10
    $null=Query 'game infighting_stats'
    # Reconstruct the preceding EX3 title format by removing only the new
    # 688-byte ownership sidecar. The native state and prior extras are exact.
    $bytes=[IO.File]::ReadAllBytes((Join-Path $root 'saves/save8.sav'))
    if([BitConverter]::ToUInt32($bytes,$bytes.Length-4) -ne 0x345845){throw 'EX4 state was not written'}
    $old=[byte[]]::new($bytes.Length-688)
    [Array]::Copy($bytes,0,$old,0,$old.Length-8)
    [Array]::Copy([byte[]](0x44,0x4f,0x4f,0x4d,0x45,0x58,0x33,0),0,$old,$old.Length-8,8)
    [IO.File]::WriteAllBytes((Join-Path $root 'saves/save9.sav'),$old)
    $null=Query 'loadstate 9';Advance 10
    $null=Query 'game infighting_stats'
    Write-Output "Infighting runtime evidence: $root"
}finally{
    $transcript|ConvertTo-Json -Depth 6|Set-Content (Join-Path $root 'tcp.json')
    if($client){$client.Dispose()}
    if($process -and !$process.HasExited){Stop-Process -Id $process.Id}
    foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}
}

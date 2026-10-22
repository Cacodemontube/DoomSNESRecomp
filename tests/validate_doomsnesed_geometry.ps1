param([string]$EvidenceDirectory='build-evidence/doomsnesed-v02-final',[string]$BuildDirectory='build-interpolation-release',[string]$Rom='Doom (USA).sfc')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$evidence=[IO.Path]::GetFullPath((Join-Path $root $EvidenceDirectory))
$run=Join-Path $evidence 'runtime'
New-Item -ItemType Directory -Force "$run/editor","$run/dumps"|Out-Null
Copy-Item -LiteralPath "$root/$BuildDirectory/DoomSNESRecomp.exe" -Destination $run
foreach($folder in @('assets','mods')){Copy-Item -LiteralPath "$root/$BuildDirectory/$folder" -Destination $run -Recurse -Force}
@'
[General]
Autosave = 0
DisableFrameDelay = 1
RunAhead = 0
[Graphics]
OutputMethod = SDL-Software
Renderer = software
WindowScale = 2
NewRenderer = 1
VSync = 0
[Sound]
EnableAudio = 0
'@|Set-Content -LiteralPath "$run/config.ini" -Encoding ascii
$boot=@'
turbo on
wait 400
press start 1
wait 150
press start 1
wait 150
press a 1
wait 150
press a 1
wait 500
'@
$variables=@{SDL_VIDEODRIVER='dummy';SDL_AUDIODRIVER='dummy';SNESRECOMP_REWIND='0';SNESRECOMP_RUN_FRAMES='5000';SNESRECOMP_DUMP_DIR="$run/dumps"};$previous=@{}
function Run-Map([string]$name,[string]$route){
    Copy-Item -LiteralPath "$evidence/$name.dse" -Destination "$run/editor/active.dse"
    ($boot+[Environment]::NewLine+$route+[Environment]::NewLine+'quit')|Set-Content -LiteralPath "$run/route.txt" -Encoding ascii
    $process=Start-Process -FilePath "$run/DoomSNESRecomp.exe" -ArgumentList @('--no-launcher','--script',('"'+$run+'/route.txt"'),'--rom',('"'+$root+'/'+$Rom+'"')) -WorkingDirectory $run -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run/$name-stdout.log" -RedirectStandardError "$run/$name-stderr.log"
    if(!$process.WaitForExit(45000)){Stop-Process -Id $process.Id;throw "$name timed out"}
    if($process.ExitCode){throw "$name failed with $($process.ExitCode)"}
}
function Dump([string]$name){return ,([IO.File]::ReadAllBytes("$run/dumps/$name.sram.bin"))}
function Word($bytes,$offset){[BitConverter]::ToInt16($bytes,$offset)}
function X($bytes){Word $bytes (([BitConverter]::ToUInt16($bytes,0x18c))+8)}
function Require($value,[string]$message){if(!$value){throw $message}}
try {
    foreach($key in $variables.Keys){$previous[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$variables[$key],'Process')}
    Run-Map 'door' @'
dump door-start
press up 120
dump door-blocked
press a 24
wait 120
dump door-open
press up 120
dump door-crossed
wait 300
dump door-closed
press a 24
wait 180
dump door-exit
press a 24
wait 400
press a 24
wait 500
dump door-next
'@
    $start=Dump 'door-start';$blocked=Dump 'door-blocked';$open=Dump 'door-open';$crossed=Dump 'door-crossed';$closed=Dump 'door-closed'
    Require ((Word $start 0x9a)-eq 3 -and (X $start)-eq -256) 'Door map did not load'
    Require ((X $blocked)-lt 0 -and (X $blocked)-gt -32) 'Closed door did not block'
    Require ((Word $open 0x3092)-eq 124) 'Door did not open to neighbor ceiling minus four'
    Require ((X $crossed)-gt 64) 'Player did not cross the opened portal'
    Require ((Word $closed 0x3092)-eq 0) 'Door did not close again'
    Require ((Word (Dump 'door-exit') 0x1e8)-eq 11) 'Exit action did not fire'
    Require ((Word (Dump 'door-next') 0x9a)-eq 200) 'Exit did not reach original E1M2'
    Run-Map 'lift' @'
dump lift-start
press up 60
dump lift-blocked
press a 24
wait 90
dump lift-down
press up 48
dump lift-enter
wait 300
dump lift-raised
'@
    Require ((Word (Dump 'lift-start') 0x3090)-eq 64) 'Lift did not start raised'
    Require ((X (Dump 'lift-blocked'))-lt 0) 'Raised lift did not block entry'
    Require ((Word (Dump 'lift-down') 0x3090)-eq 0) 'Lift did not lower'
    Require ((X (Dump 'lift-enter'))-gt 32) 'Player did not enter lowered lift'
    Require ((Word (Dump 'lift-raised') 0x3090)-eq 64) 'Lift did not raise again'
    Run-Map 'convex' "dump convex-start`npress up 240`nwait 30`ndump convex-wall"
    Require ((Word (Dump 'convex-start') 0x9a)-eq 1) 'Convex map did not load'
    Require ((X (Dump 'convex-wall'))-gt 480 -and (X (Dump 'convex-wall'))-lt 512) 'Convex map collision failed'
    Run-Map 'drawn' "dump drawn-start`npress up 240`nwait 30`ndump drawn-portal"
    Require ((Word (Dump 'drawn-start') 0x9a)-eq 2) 'Concave map did not load'
    Require ((X (Dump 'drawn-portal'))-gt 512) 'Concave BSP did not permit travel through shared portal'
    Run-Map 'effect-7' "press up 60`npress a 24`nwait 120`ndump stairs-raised"
    Require ((Word (Dump 'stairs-raised') 0x3090)-eq 72) 'Native stairs did not raise eight units'
    Run-Map 'effect-18' "press a 24`nwait 150`ndump floor-raised"
    Require ((Word (Dump 'floor-raised') 0x3090)-eq 64) 'Native floor did not reach the next higher neighbor'
    Run-Map 'effect-73' "press up 80`nwait 90`ndump crusher-lowered"
    Require ((Word (Dump 'crusher-lowered') 0x3092)-lt 192) 'Native walk trigger did not lower crusher ceiling'
    if(Test-Path -LiteralPath "$evidence/hangar-rebuilt.dse"){
        Run-Map 'hangar-rebuilt' "dump hangar-start`npress up 60`ndump hangar-moved"
        Require ((Word (Dump 'hangar-start') 0x9a)-eq 85) 'Rebuilt original E1M1 did not load'
        $first=Dump 'hangar-start';$last=Dump 'hangar-moved';$player=[BitConverter]::ToUInt16($first,0x18c)
        Require ((X $first)-ne (X $last) -or (Word $first ($player+12))-ne (Word $last ($player+12))) 'Rebuilt original map did not permit player movement'
    }
    'PASS: native door collision/open/traverse/close/exit; lift lower/enter/raise; convex room collision; concave BSP portal traversal; stairs/floor raise/crusher; rebuilt original E1M1.'|Set-Content -LiteralPath "$evidence/geometry-runtime-results.txt"
    Get-Content -LiteralPath "$evidence/geometry-runtime-results.txt"
} finally {foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}}

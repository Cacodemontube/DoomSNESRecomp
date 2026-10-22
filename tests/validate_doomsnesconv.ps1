param([string]$BuildDirectory='build',[string]$EvidenceDirectory='build-evidence/converter-04',[string]$Rom='Doom (USA).sfc')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$evidence=[IO.Path]::GetFullPath((Join-Path $root $EvidenceDirectory))
$build=[IO.Path]::GetFullPath((Join-Path $root $BuildDirectory))
$run=Join-Path $evidence 'runtime'
New-Item -ItemType Directory -Force $run,"$run/dumps" | Out-Null
Copy-Item "$build/DoomSNESRecomp.exe" $run
foreach($folder in @('assets','mods')){Copy-Item "$build/$folder" $run -Recurse -Force}
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
'@ | Set-Content "$run/config.ini"
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
$previous=@{};$variables=@{SDL_VIDEODRIVER='dummy';SDL_AUDIODRIVER='dummy';SNESRECOMP_REWIND='0';SNESRECOMP_RUN_FRAMES='6500';SNESRECOMP_DUMP_DIR="$run/dumps"}
function Require($value,[string]$message){if(!$value){throw $message}}
function Run-Campaign([string]$name,[string]$patch,[string]$route){
    ($boot+[Environment]::NewLine+$route+[Environment]::NewLine+'quit') | Set-Content "$run/route.txt"
    $p=Start-Process -FilePath "$run/DoomSNESRecomp.exe" -ArgumentList @('--no-launcher','--script',('"'+$run+'/route.txt"'),'--rom',('"'+$root+'/'+$Rom+'"'),'--campaign',('"'+$patch+'"')) -WorkingDirectory $run -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run/$name-stdout.log" -RedirectStandardError "$run/$name-stderr.log"
    if(!$p.WaitForExit(45000)){Stop-Process -Id $p.Id;throw "$name timed out"}
    Require (!$p.ExitCode) "$name game failed"
    Require ((Get-Content "$run/$name-stderr.log" -Raw).Contains('verified level patch prepared')) "$name patch was not applied"
}
function Level([string]$tag){$b=[IO.File]::ReadAllBytes("$run/dumps/$tag.wram.bin");return $b[0x6d1]}
function Word([string]$tag,[int]$offset){$b=[IO.File]::ReadAllBytes("$run/dumps/$tag.sram.bin");return [BitConverter]::ToUInt16($b,$offset)}
try {
    foreach($key in $variables.Keys){$previous[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$variables[$key],'Process')}
    Run-Campaign 'single' "$evidence/native-campaign.sfc" @'
dump single-start
press up 150
press a 24
wait 650
dump single-ending
'@
    Require ((Word 'single-start' 0x9a)-eq 2) 'Converted level did not load'
    Require ((Level 'single-ending')-eq 27 -and (Word 'single-ending' 0x1e8)-eq 11) 'Single use exit did not reach native episode 3 ending'
    Run-Campaign 'walk' "$evidence/walk-campaign.dse" @'
press up 150
wait 650
dump walk-ending
'@
    Require ((Level 'walk-ending')-eq 27) 'Converted PC secret walk exit did not reach native ending without Use'
    # Export a full two-level sidecar with the same public converter entry point.
    $p=Start-Process -FilePath "$build/DoomSNESconv.exe" -ArgumentList @('--convert',('"'+$root+'/'+$Rom+'"'),('"'+$evidence+'/converter-fixture.wad"'),('"'+$evidence+'/native-multi.sfc"'),'--merge') -WindowStyle Hidden -Wait -PassThru
    Require (!$p.ExitCode) 'Two-level conversion failed'
    Run-Campaign 'multi' "$evidence/native-multi.dse" @'
press up 150
press a 24
wait 300
press a 24
wait 600
press a 24
wait 600
dump multi-second
press up 150
press a 24
wait 650
dump multi-ending
'@
    Require ((Level 'multi-second')-ne 0 -and (Level 'multi-second')-ne 27) 'Campaign did not advance to the second selected map'
    Require ((Level 'multi-ending')-eq 27) 'Second selected map did not reach the final ending'
    if(Test-Path "$evidence/converted-e1m1.dse"){
        Run-Campaign 'alias' "$evidence/converted-e1m1.dse" "dump alias-start`npress up 45`ndump alias-moved"
        Require ((Word 'alias-start' 0x9a)-eq 88) 'Larger-slot PC E1M1 alias failed to load 88 sectors'
    }
    'PASS: converted level boots; .sfc companion selection works; use exit reaches actual episode 3 ending; PC secret walk exit replacement reaches ending without Use; two selected levels run in order and finish; larger-slot alias checked when present.' | Set-Content "$evidence/native-runtime-results.txt"
    Get-Content "$evidence/native-runtime-results.txt"
} finally {foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}}

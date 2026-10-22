param(
    [string]$BuildDirectory = 'build-interpolation-release',
    [string]$Rom = 'Doom (USA).sfc',
    [string]$RunLabel = 'doomsnesed-validation'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$build = [IO.Path]::GetFullPath((Join-Path $root $BuildDirectory))
$romPath = (Resolve-Path -LiteralPath (Join-Path $root $Rom)).Path
if($RunLabel -notmatch '^[a-zA-Z0-9_-]+$'){throw 'Invalid run label.'}
$evidence = Join-Path $root "build-evidence/$RunLabel"
if(Test-Path -LiteralPath $evidence){throw "Evidence already exists: $evidence"}
New-Item -ItemType Directory -Force "$evidence/runtime/editor","$evidence/runtime/dumps" | Out-Null
& powershell -NoProfile -ExecutionPolicy Bypass -File "$root/editor/build_editor.ps1"
if($LASTEXITCODE){throw 'Editor build failed.'}
$test = Start-Process -FilePath "$root/build/DoomSNESed.exe" -ArgumentList @('--self-test',('"'+$romPath+'"'),('"'+$evidence+'"')) -WindowStyle Hidden -Wait -PassThru
if($test.ExitCode){throw (Get-Content "$evidence/results.txt" -Raw)}
Copy-Item -LiteralPath "$build/DoomSNESRecomp.exe" -Destination "$evidence/runtime"
foreach($folder in @('assets','mods')) {Copy-Item -LiteralPath "$build/$folder" -Destination "$evidence/runtime" -Recurse}
Copy-Item -LiteralPath "$evidence/room-empty.dse" -Destination "$evidence/runtime/editor/active.dse"
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
'@ | Set-Content -LiteralPath "$evidence/runtime/config.ini" -Encoding ascii
@'
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
dump room-start
press up 240
wait 30
dump wall-stop
press a 24
wait 180
dump used-exit
press a 24
wait 400
press a 24
wait 500
dump next-map
quit
'@ | Set-Content -LiteralPath "$evidence/runtime/route.txt" -Encoding ascii
$previous = @{}
$variables = @{SDL_VIDEODRIVER='dummy';SDL_AUDIODRIVER='dummy';SNESRECOMP_REWIND='0';SNESRECOMP_RUN_FRAMES='4000';SNESRECOMP_DUMP_DIR="$evidence/runtime/dumps"}
$process = $null
try {
    foreach($key in $variables.Keys){$previous[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$variables[$key],'Process')}
    $arguments=@('--no-launcher','--script',('"'+$evidence+'/runtime/route.txt"'),'--rom',('"'+$romPath+'"'))
    $process=Start-Process -FilePath "$evidence/runtime/DoomSNESRecomp.exe" -ArgumentList $arguments -WorkingDirectory "$evidence/runtime" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$evidence/runtime/stdout.log" -RedirectStandardError "$evidence/runtime/stderr.log"
    if(!$process.WaitForExit(45000)){throw 'Native room test timed out.'}
    if($process.ExitCode){throw "Native room game failed: $($process.ExitCode)"}
    function Read-Dump([string]$tag){return ,([IO.File]::ReadAllBytes("$evidence/runtime/dumps/$tag.sram.bin"))}
    $start=Read-Dump 'room-start';$wall=Read-Dump 'wall-stop';$used=Read-Dump 'used-exit';$next=Read-Dump 'next-map'
    $player=[BitConverter]::ToUInt16($start,0x18c)
    if([BitConverter]::ToUInt16($start,0x9a) -ne 1){throw 'New room did not load one sector.'}
    if([BitConverter]::ToInt16($start,$player+8) -ne 0){throw 'New player start was not loaded.'}
    if([BitConverter]::ToInt16($wall,$player+8) -ne 496){throw 'Room collision did not stop the player at the east wall.'}
    if([BitConverter]::ToUInt16($used,0x1e8) -ne 11){throw 'Native use-to-exit action did not fire.'}
    if([BitConverter]::ToUInt16($next,0x9a) -ne 200){throw 'Exit did not reach the original E1M2.'}
    'PASS: new room boots; player starts at 0,0; collision stops at x=496; native exit fires and reaches E1M2.' | Set-Content "$evidence/runtime-results.txt"
    Get-Content "$evidence/results.txt","$evidence/runtime-results.txt"
} finally {
    if($process -and !$process.HasExited){Stop-Process -Id $process.Id}
    foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}
}

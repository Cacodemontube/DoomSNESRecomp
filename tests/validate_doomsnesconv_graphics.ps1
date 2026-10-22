param([string]$EvidenceDirectory='build-evidence/converter-04')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$evidence=[IO.Path]::GetFullPath((Join-Path $root $EvidenceDirectory))
$run=Join-Path $evidence 'graphics-runtime'
New-Item -ItemType Directory -Force $run,"$run/dumps" | Out-Null
Copy-Item "$root/build/DoomSNESRecomp.exe" $run
Copy-Item "$root/build/assets" $run -Recurse -Force
@'
[General]
Autosave = 0
DisableFrameDelay = 1
[Graphics]
OutputMethod = SDL-Software
Renderer = software
VSync = 0
[Sound]
EnableAudio = 0
'@ | Set-Content "$run/config.ini"
@'
turbo on
wait 120
dump logo-120
wait 120
dump logo-240
wait 120
dump title-360
wait 120
dump title-480
wait 120
dump title-600
press start 1
wait 150
press start 1
wait 150
press a 1
wait 150
press a 1
wait 500
dump graphics-map
quit
'@ | Set-Content "$run/script.txt"
$previous=@{}
$variables=@{SDL_VIDEODRIVER='dummy';SDL_AUDIODRIVER='dummy';SNESRECOMP_REWIND='0';SNESRECOMP_RUN_FRAMES='3000';SNESRECOMP_DUMP_DIR="$run/dumps"}
try {
    foreach($key in $variables.Keys){$previous[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$variables[$key],'Process')}
    $p=Start-Process "$run/DoomSNESRecomp.exe" -ArgumentList @('--no-launcher','--script',('"'+$run+'/script.txt"'),'--rom',('"'+$root+'/Doom (USA).sfc"'),'--campaign',('"'+$evidence+'/native-screens.dse"')) -WorkingDirectory $run -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run/stdout.log" -RedirectStandardError "$run/stderr.log"
    if(!$p.WaitForExit(45000)){Stop-Process -Id $p.Id;throw 'Graphics campaign timed out'}
    if($p.ExitCode){throw 'Graphics campaign failed'}
    if(!(Get-Content "$run/stderr.log" -Raw).Contains('verified level patch prepared')){throw 'Graphics patch was not accepted'}
    $found=$false
    foreach($file in Get-ChildItem "$run/dumps/*.vram.bin"){
        $vram=[IO.File]::ReadAllBytes($file.FullName)
        # Imported quadrant image: green upper left and red lower right.
        $map=[BitConverter]::ToUInt16($vram,0xf800)
        $lower=[BitConverter]::ToUInt16($vram,0xf800+(14*32+16)*2)
        if($map -ne $lower){$found=$true}
    }
    if(!$found){throw 'Replacement tile map was not transferred to native VRAM'}
    Add-Type -AssemblyName System.Drawing
    foreach($tag in @('title-360','title-600')){
        $bitmap=[Drawing.Bitmap]::FromFile("$run/dumps/$tag.fb.bmp")
        try {
            $upper=$bitmap.GetPixel(50,50);$lower=$bitmap.GetPixel(200,180)
            if($upper.G -lt 200 -or $upper.R -gt 20 -or $lower.R -lt 200 -or $lower.G -gt 20){throw "Native $tag artwork is missing or vertically inverted"}
        } finally {$bitmap.Dispose()}
    }
    $sram=[IO.File]::ReadAllBytes("$run/dumps/graphics-map.sram.bin")
    if([BitConverter]::ToUInt16($sram,0x9a)-ne 2){throw 'Graphics campaign did not enter its converted level'}
    'PASS: native graphics patch accepted; native logo/title VRAM transfers; converted campaign enters gameplay. Screenshots and memory dumps saved.' | Set-Content "$evidence/native-graphics-results.txt"
} finally {foreach($key in $variables.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}}

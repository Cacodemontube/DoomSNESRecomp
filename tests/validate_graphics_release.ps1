param([string]$BinaryDirectory='build-interpolation-release',[string]$RunLabel='release-fixed',[switch]$Rotations,[switch]$Crosshair,[switch]$Infighting)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$source=(Resolve-Path (Join-Path $repo $BinaryDirectory)).Path
$root=Join-Path $repo "build-evidence/graphics-fix/$RunLabel"
if(Test-Path -LiteralPath $root){throw 'Evidence run already exists'}
New-Item -ItemType Directory -Path (Join-Path $root 'captures') | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'DoomSNESRecomp.exe') -Destination $root
foreach($dir in @('assets','mods')){Copy-Item -LiteralPath (Join-Path $source $dir) -Destination $root -Recurse}
$config=Get-Content (Join-Path $repo 'dist/DoomSNESRecomp-Jitter-Fix-Local-20261015-Windows-x64/config.ini') -Raw
$config=$config.Replace('Renderer = auto','Renderer = software').Replace('OutputMethod = SDL','OutputMethod = SDL-Software').Replace('EnableAudio = 1','EnableAudio = 0').Replace('DisableFrameDelay = 0','DisableFrameDelay = 1')
Set-Content (Join-Path $root 'config.ini') $config -Encoding ascii
$state=@('format_version = 1')
foreach($pair in @(@('doom.episodes','unlocked-episodes'),@('doom.presentation','widescreen'))){
    $state+=@('[[package]]',('id = "'+$pair[0]+'"'),'version = "1.0.0"','[[feature]]',('package_id = "'+$pair[0]+'"'),('id = "'+$pair[1]+'"'),'enabled = true')
    if($pair[1] -eq 'widescreen'){$state+=@('[feature.values]','aspect = "16:9"')}
}
if($Rotations){$state+=@('[[package]]','id = "doom.rotations"','version = "1.0.0"','[[feature]]','package_id = "doom.rotations"','id = "pc-rotations"','enabled = true','[[resource]]','package_id = "doom.rotations"','feature_id = "pc-rotations"','id = "doom-wad"',('path = "'+(($repo+'/DOOM.WAD') -replace '\\','/')+'"'))}
if($Crosshair){$state+=@('[[package]]','id = "doom.crosshair"','version = "1.0.0"','[[feature]]','package_id = "doom.crosshair"','id = "autoaim-crosshair"','enabled = true')}
if($Infighting){$state+=@('[[package]]','id = "doom.infighting"','version = "1.0.0"','[[feature]]','package_id = "doom.infighting"','id = "monster-infighting"','enabled = true')}
Set-Content (Join-Path $root 'mods/preloaded/state.toml') $state -Encoding ascii
Set-Content (Join-Path $root 'route.txt') @('wait 400','press start 1','wait 150','press start 1','wait 150','press a 1','wait 150','press down 12','wait 24','press down 12','wait 24','press a 1','wait 500','press right 120','wait 120','quit') -Encoding ascii
$previous=@{}
try{
    $envs=@{SDL_VIDEODRIVER='dummy';SDL_AUDIODRIVER='dummy';SNESRECOMP_RUN_FRAMES='1800';SNESRECOMP_REWIND='0';DOOM_RENDER_STATS='1';SNESRECOMP_SCREENSHOT_DIR=(Join-Path $root 'captures');SNESRECOMP_SCREENSHOT_FROM='1450';SNESRECOMP_SCREENSHOT_TO='1550'}
    foreach($key in $envs.Keys){$previous[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$envs[$key],'Process')}
    $arguments=@('--no-launcher','--script',('"'+(Join-Path $root 'route.txt')+'"'),'--rom',('"'+(Join-Path $repo 'Doom (USA).sfc')+'"'))
    $process=Start-Process -FilePath (Join-Path $root 'DoomSNESRecomp.exe') -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $root 'stdout.log') -RedirectStandardError (Join-Path $root 'stderr.log')
    if($process.ExitCode){throw "Runtime failed: $($process.ExitCode)"}
    Get-Content (Join-Path $root 'stderr.log') -Tail 3
    Write-Output "Release captures: $root"
}finally{foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}}

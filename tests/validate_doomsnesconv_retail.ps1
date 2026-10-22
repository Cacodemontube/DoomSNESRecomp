param([string]$EvidenceDirectory='build-evidence/converter-06/audio-06',[string]$BuildDirectory='build')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$build=if([IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory}else{Join-Path $root $BuildDirectory}
$evidence=[IO.Path]::GetFullPath((Join-Path $root $EvidenceDirectory))
$run=Join-Path $evidence 'retail-runtime'
New-Item -ItemType Directory -Force $run,"$run/dumps" | Out-Null
Copy-Item -LiteralPath "$build/DoomSNESRecomp.exe" -Destination $run
Copy-Item -LiteralPath "$build/assets" -Destination $run -Recurse -Force
@'
[General]
Autosave = 0
DisableFrameDelay = 1
[Graphics]
OutputMethod = SDL-Software
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
dump retail-start
'@
$vars=@{SDL_VIDEODRIVER='dummy';SDL_AUDIODRIVER='dummy';SNESRECOMP_REWIND='0';SNESRECOMP_RUN_FRAMES='4500';SNESRECOMP_DUMP_DIR="$run/dumps";SNESRECOMP_DUMP_AUDIO='1'};$prior=@{}
try {
 foreach($key in $vars.Keys){$prior[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$vars[$key],'Process')}
 function Run([string]$name,[string]$route){($boot+[Environment]::NewLine+$route+[Environment]::NewLine+'quit') | Set-Content "$run/script.txt"
  $process=Start-Process "$run/DoomSNESRecomp.exe" -ArgumentList @('--no-launcher','--rom',('"'+$root+'/Doom (USA).sfc"'),'--campaign',('"'+$evidence+'/'+$name+'.dse"'),'--script',('"'+$run+'/script.txt"')) -WorkingDirectory $run -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run/$name-stdout.log" -RedirectStandardError "$run/$name-stderr.log"
  if(!$process.WaitForExit(45000)){Stop-Process -Id $process.Id;throw "$name timed out"};if($process.ExitCode){throw "$name failed: $(Get-Content "$run/$name-stderr.log" -Raw)"}
  if(!(Get-Content "$run/$name-stderr.log" -Raw).Contains('native level data applied before frame 1')){throw 'Native asset patch not applied'}
 }
 Run 'retail-assets' "press y 30`ndump sample-fired"
 $state=[IO.File]::ReadAllBytes("$run/dumps/retail-start.sram.bin");if([BitConverter]::ToUInt16($state,0x9a)-ne85){throw 'Original E1M1 geometry changed or did not load'}
 $setup=Get-Content "$evidence/retail-assets.sned" -Raw | ConvertFrom-Json
 $apu=[IO.File]::ReadAllBytes("$run/dumps/retail-start.aram.bin");$song=$setup.Audio.Music[0];$origin=$song.Settings.Origin
 for($i=0;$i-lt $song.Sequence.Count;$i++){if($apu[$origin+$i]-ne $song.Sequence[$i]){throw 'Expanded ROM music did not reach SPC RAM'}}
 $dsp=[IO.File]::ReadAllBytes("$run/dumps/retail-start.dsp.bin");$active=0;for($c=0;$c-lt8;$c++){if($dsp[$c*16+8]){$active++}};if(!$active){throw 'Expanded native music has no active DSP voices'}
 $fired=[BitConverter]::ToString([IO.File]::ReadAllBytes("$run/dumps/sample-fired.aram.bin"));$sample=[BitConverter]::ToString([byte[]]$setup.Audio.Sounds[0].Data);if(!$fired.Contains($sample)){throw 'Expanded native replacement WAV did not stream'}
 Add-Type -AssemblyName System.Drawing
 foreach($episode in 1..3){$route="press up 150`npress a 24`nwait 650`ndump episode-$episode";if($episode-eq3){$route+="`npoke 73a 0300`npress a 1`nwait 180`ndump credits-1"};Run "episode-$episode" $route
   $image=[Drawing.Bitmap]::FromFile("$run/dumps/episode-$episode.fb.bmp")
   try{$top=$image.GetPixel(20,20);$bottom=$image.GetPixel(200,180);if($top.G-lt30 -or $top.R-gt50 -or $bottom.B-lt50 -or $bottom.R-gt50){throw "Episode $episode artwork missing/inverted: $top $bottom"}}finally{$image.Dispose()}
   $vram=[IO.File]::ReadAllBytes("$run/dumps/episode-$episode.vram.bin");$tile=[BitConverter]::ToUInt16($vram,0xf000+(4*32+1)*2);if(($tile-band1023)-ne([int][char]'D'-32)){throw "Episode $episode custom story text not displayed with the native font"}
 }
 # USA retail increments TitleCount on entry. Force each native credits-cycle
 # count through WRAM, without changing ROM code or the exported progression.
 Run 'episode-3' "press up 150`npress a 24`nwait 650`npoke 73a 0700`npress a 1`nwait 180`ndump credits-2"
 foreach($page in 1..2){$vram=[IO.File]::ReadAllBytes("$run/dumps/credits-$page.vram.bin");$base=if($page-eq1){0xe000}else{0xe800};$tile=[BitConverter]::ToUInt16($vram,$base+(2*32+1)*2);if(($tile-band1023)-ne[int][char]'D'){throw "Credits page $page custom text did not reach the native font tile map"}}
 'PASS: expanded original-ROM assets boot in native recomp; original E1M1 geometry preserved; converted music reaches SPC and plays; replacement WAV streams; all three native episode intermission images and ending texts display; both native credits pages show their independent replacement strings.' | Set-Content "$evidence/native-retail-results.txt"
} finally {foreach($key in $prior.Keys){[Environment]::SetEnvironmentVariable($key,$prior[$key],'Process')}}

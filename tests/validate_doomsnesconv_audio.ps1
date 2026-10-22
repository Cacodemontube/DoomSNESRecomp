param([string]$EvidenceDirectory='build-evidence/converter-04/audio-05',[string]$FullScorePatch='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$evidence=[IO.Path]::GetFullPath((Join-Path $root $EvidenceDirectory))
$run=Join-Path $evidence 'runtime'
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
wait 400
press start 1
wait 150
press start 1
wait 150
press a 1
wait 150
press a 1
wait 500
dump music-start
wait 7
dump music-progress
press y 30
dump sample-fired
wait 15
dump sample-finished
quit
'@ | Set-Content "$run/script.txt"
Add-Type -TypeDefinition @'
using System;
public static class NativeAudioCheck {
 public static int Find(byte[] bytes,byte[] part){for(int p=0;p<=bytes.Length-part.Length;p++){if(bytes[p]!=part[0])continue;int i=1;while(i<part.Length&&bytes[p+i]==part[i])i++;if(i==part.Length)return p;}return -1;}
 public static bool At(byte[] bytes,int p,byte[] part){if(p<0||p+part.Length>bytes.Length)return false;for(int i=0;i<part.Length;i++)if(bytes[p+i]!=part[i])return false;return true;}
}
'@
$previous=@{}
$variables=@{SDL_VIDEODRIVER='dummy';SDL_AUDIODRIVER='dummy';SNESRECOMP_REWIND='0';SNESRECOMP_RUN_FRAMES='3000';SNESRECOMP_DUMP_DIR="$run/dumps";SNESRECOMP_DUMP_AUDIO='1'}
try {
    foreach($key in $variables.Keys){$previous[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$variables[$key],'Process')}
    $cases=@('native-audio','native-mus');if($FullScorePatch){$cases+='full-score'}
    foreach($kind in $cases){
        $patch=if($kind-eq'full-score'){[IO.Path]::GetFullPath($FullScorePatch)}else{"$evidence/$kind.dse"}
        $p=Start-Process "$run/DoomSNESRecomp.exe" -ArgumentList @('--no-launcher','--script',('"'+$run+'/script.txt"'),'--rom',('"'+$root+'/Doom (USA).sfc"'),'--campaign',('"'+$patch+'"')) -WorkingDirectory $run -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run/$kind-stdout.log" -RedirectStandardError "$run/$kind-stderr.log"
        if(!$p.WaitForExit(45000)){Stop-Process -Id $p.Id;throw "$kind timed out"}
        if($p.ExitCode){throw "$kind failed: $(Get-Content "$run/$kind-stderr.log" -Raw)"}
        if(!(Get-Content "$run/$kind-stderr.log" -Raw).Contains('verified level patch prepared')){throw 'Native audio patch was not accepted'}
        $apu=[IO.File]::ReadAllBytes("$run/dumps/music-start.aram.bin")
        $dsp=[IO.File]::ReadAllBytes("$run/dumps/music-start.dsp.bin")
        $active=0;for($voice=0;$voice-lt 8;$voice++){if($dsp[$voice*16+8]-gt0){$active++}}
        if(!$active){throw "$kind has no active DSP envelope"}
        $state=[IO.File]::ReadAllBytes("$run/dumps/music-start.sram.bin")
        if([BitConverter]::ToUInt16($state,0x9a)-ne2){throw "$kind did not enter the converted level"}
        if($kind-eq'native-audio'){
            $project=Get-Content "$evidence/native-audio.sned" -Raw | ConvertFrom-Json
            if(![NativeAudioCheck]::At($apu,0x8fe9,[byte[]]$project.Audio.Music[0].Sequence)){throw 'Converted MIDI sequence was not uploaded to SPC RAM'}
            [Reflection.Assembly]::LoadFrom("$root/build/DoomSNESconv.exe") | Out-Null
            $rom=[IO.File]::ReadAllBytes("$root/Doom (USA).sfc")
            $slots=[DoomSNESed.AudioRom]::Sounds($rom,"$root/build/editor-data")
            $door=$slots | Where-Object Wave -eq 6
            $resident=$project.Audio.Sounds | Where-Object Wave -eq 6
            if(![NativeAudioCheck]::At($apu,$door.Address,[byte[]]$resident.Data)){throw 'Resident door waveform was not uploaded'}
            $fired=[IO.File]::ReadAllBytes("$run/dumps/sample-fired.aram.bin")
            $pistol=$project.Audio.Sounds | Where-Object Wave -eq 10
            if([NativeAudioCheck]::Find($fired,[byte[]]$pistol.Data)-lt0){throw 'Replacement pistol waveform was not streamed into the native blast buffer'}
            Copy-Item "$run/dumps/music-start.aram.bin" "$evidence/midi-loaded.aram.bin"
            Copy-Item "$run/dumps/music-start.dsp.bin" "$evidence/midi-playing.dsp.bin"
        }
        if($kind-eq'full-score'){
            $score=Get-Content ([IO.Path]::ChangeExtension($patch,'.sned')) -Raw | ConvertFrom-Json
            if(![NativeAudioCheck]::At($apu,0x8fe9,[byte[]]$score.Audio.Music[0].Sequence)){throw 'Full imported score did not reach SPC RAM'}
        }
    }
    'PASS: MIDI and MUS campaigns boot natively; DSP voices play; custom MIDI sequence and resident WAV reach SPC RAM; firing streams replacement pistol BRR through the retail turbo driver.' | Set-Content "$evidence/native-runtime-results.txt"
} finally {foreach($key in $variables.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}}

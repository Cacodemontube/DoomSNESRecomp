# Recording and audio refill audit

## Remaining investigation

The later replacement root videos last 28.35 and 22.93 seconds; the earlier
durations below describe the previous files, not these replacements.

To attribute a remaining click, capture the same uninterrupted E1M1 route
at three boundaries: native 32040 Hz DSP PCM, the final resampled callback
buffer supplied to SDL, and uncompressed Windows loopback/device output.
The existing native PCM history covers the first boundary; the final SDL
buffer and loopback need simultaneous capture. Record FIFO occupancy,
underflows/drops and callback/frame timing with a shared clock. Buffer data
in memory and write it after playback so capture I/O cannot create clicks.

Compare stock and enhanced presentation at native and 4x resolution, first
with an idle scene and then movement/combat. Compare a matched original-ROM
reference route. If the native PCM contains the extra click, align DSP register
writes and note transitions before blaming BRR decoding. If it first appears
in the callback, investigate resampling, clock correction and refill recovery.
If only loopback contains it, investigate SDL/device conversion and scheduling.
Compressed Game DVR audio alone cannot distinguish these stages.

The current decoder and idle-delivery checks narrow the search; they do not
prove the reported crackle fixed, nor clear busy gameplay of starvation.

## 2026-10-11: E1M1 follow-up

The new root-level `vid.mp4` and `vid2.mp4` were inspected separately from
the older recording described below. They last 22.17 and 16.25 seconds.
Their missing-menu state is reproduced by switching the PPU with the R
keyboard shortcut; the fix also removes the extra reference-raster cost.

A paced 16:9/4x E1M1 run on the actual Realtek output device, with VSync
configured on, reported zero output underflows, zero missing frames and
zero audible drops at the observation point near field 2000. The audio
callback produced zero guest samples: production remained guest-driven.
The native BRR decoder and echo FIR matched the in-process reference
exactly. Gaussian interpolation differed by at most three integer sample
units (-80.77 dBFS), which does not explain large crackles.

The Snes9x reference and native PCM both contain substantial adjacent-sample
transients during E1M1. Cross-emulator waveform alignment is not close
enough to attribute a specific recorded click to the decoder. These checks
do **not** establish that the reported audible crackling is fully fixed.
No speculative filtering or change to the game's music was applied.

`tests/validate_audio_delivery.ps1 -Desktop` now observes uninterrupted
E1M1 playback, retains native PCM and checks delivery/reference counters.
It runs beyond both observation points: pausing at a TCP checkpoint while
the device drains the FIFO can itself manufacture an apparent underflow.
Local evidence includes `audio-live-final-20261011/audio-summary.json`,
`menu-audit-20261011/dsp-reference.json` and the reference/native WAVs.

The committed playback check also passed on the real output device at
field 2003 (`audio-delivery-regression-final-20261011`). A production
movement/pause run retained its visible menu with a legacy PPU configured.
Its audio counters stayed at zero through gameplay before screenshot
capture. Writing full-resolution PPM captures then blocked composition for
approximately 100 ms per capture and caused eight starvation episodes.
Those are diagnostic disk-I/O stalls, not evidence that ordinary gameplay
or the supplied Game DVR recordings have the same cause. Playback counters
should be measured without synchronous screenshots.

The supplied `build/vid.mp4` contains 18.38 seconds of stereo AAC audio at
48 kHz. Decoding to PCM shows sharp adjacent-sample jumps around 8.020 and
8.670 seconds; peak level is about -0.74 dBFS, without sustained clipping.
These observations locate audible discontinuities but cannot by themselves
identify their source, because this is a compressed recording of device audio.

The desktop audio callback had a second refill gate in addition to the native
consumer's refill logic. After starvation near the end of a device block,
the native consumer could leave its 64-sample fade unfinished. The desktop
gate then replaced the next block with zeroes while waiting for the FIFO to
refill, abruptly cutting that fade. The callback now always calls
`RtlRenderAudio`; the native consumer owns priming, fading and recovery.
This addresses a concrete cause of clicks during buffer starvation, rather
than changing the game's samples or masking clipping with a volume change.

`snesrecomp/tests/audio_delivery/run.ps1` passes the real native-consumer
regressions for output rates, priming, starvation, recovery and reset. The
new case exhausts the FIFO at the end of a callback and checks continuity
across the next callback: a constant 10000-amplitude signal ramps with steps
no larger than 157 instead of jumping to silence.

Visible music validation now sets `DisableFrameDelay=0`. Previously those
tests used accelerated emulation while playing audio through a real device,
which could itself overflow or disrupt the audio timeline. Headless checks
retain accelerated execution with dummy audio.

All 14 release CTest checks passed after rebuilding. This diagnosis does not
establish that every discontinuity in the supplied recording has the same
cause, or rule out scheduling stalls on other hardware.

## Follow-up recording: vid5.mp4

The 22.72-second decoded recording peaks at 28913 (-1.09 dBFS); the largest
adjacent sample jump is 7921, around 19.760 seconds. Other strong jumps occur
around 18.726, 19.042 and 20.760 seconds. There is no evidence of sustained
hard clipping. These are candidate investigation windows, not proof that every
percussion transient is a playback click. Comparing one-second windows with
the original-ROM recording yields inconsistent alignment and correlations
of only 0.30–0.60, insufficient to attribute the clicks to the DSP or output.

The desktop now supports `SNESRECOMP_AUDIO_OUTPUT_WAV=<path>` to capture up
to sixty seconds of the actual post-consumer, post-volume S16 stereo samples
supplied to SDL. Capture storage is allocated before playback; the callback
only copies to memory, and the WAV is written after the audio device closes.
Exit normally to flush it. This separates native DSP generation from native
rate conversion/fading/mixing without diagnostic disk stalls. It does not
capture SDL's final device-rate converter or Windows processing; those still
require a synchronized device loopback recording. Capture is disabled unless
the environment variable is set. The remaining audible crackle is not yet
proven fixed.

The instrumented real-device E1M1 run `audio-output-trace-final` recorded
44.10 seconds of SDL input plus ten seconds of overlapping native PCM.
There were no new output underflows or dropped audible samples between fields
1400 and 2002. BRR and echo reference samples match exactly; Gaussian differences
remain at most three units. Five independently aligned 250-ms windows of native
PCM versus SDL input correlate at 0.982–0.995 with approximately unity gain,
after allowing the consumer's bounded 0.5% clock correction. This is a sampled
check of the conversion stage, not proof that the entire output is click-free.
Evidence includes `audio-summary.json`, `desktop-output.wav`,
`native-music.wav` and `pipeline-comparison.json`. The next distinguishing
capture needs synchronized Windows device loopback and original-ROM SPC timing
comparison; the supplied AAC alone cannot resolve those stages.

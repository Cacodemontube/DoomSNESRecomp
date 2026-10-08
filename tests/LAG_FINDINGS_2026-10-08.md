# Doom lag investigation, 2026-10-08

The enabled display enhancements reproduce additional host stutter. The
stock build also stretches host pacing beyond the console field duration.
The game uses whole-program CPU interpretation, and its original rendered
picture updates much less frequently than the console's roughly 60 Hz field
rate. These are separate contributors.

The apparent autonomous movement during this investigation was the explicit
test route. A separate neutral-input run kept the camera stationary for 600
fields: X=1056, Y=-3616, Z=42, angle=0x4000 at both endpoints. TCP reported
P1/P2 input zero and no controller overrides. Movement outside scripted tests
was not reproduced; this does not rule out an intermittent physical input
issue in an ordinary player session.

## Identity and evidence

- Game source: `6fa275434584e69bd64e758761673cba1132ce59` plus the diagnostic changes.
- Framework: `ec8a3e7cc2abdfb31382977c38837d6d29e937eb`.
- Production executable measured: SHA256 `29DE4C44E440163015A83D142C1F586E0372A2D35C88EC4DDB76971A7B0DDFF1`.
- US ROM: SHA256 `D45E26EB10C323ECD480E5F2326B223E29264C3ADDE67F48F0D2791128E519E8`.
- Release, GCC 16.2, Windows x64, HLE frame compositor.
- Reference: Snes9x Libretro `1.63 fae2fea`, core ZIP SHA256 `C1A4804051AD60F1FB313C514F642872489853943AFAD1D1B60444DEBF44A553`.
- Local ignored artifacts: `build-lag-evidence/` and `build-lag-reference/`.

The reference frontend was built from `snesrecomp/tools/snesref` using SDL2
2.30.9. The stock core lacks patched register/PPU-journal exports; framebuffer,
WRAM, VRAM, and SRAM captures worked. It reached visible E1M1 gameplay.

## Rendering cost

Five alternating stock/enhanced pairs used the preserved production binary,
physical input disabled, no concurrent build, dummy video/audio, uncapped
pacing, no screenshots or state trace. Each run reached gameplay on the
canonical input route. Fields 1400–1900 define the measured workload.

| Median of five runs | Stock | Fit + interpolated Auto |
| --- | ---: | ---: |
| Completed-present interval | 5.109 ms | 12.427 ms |
| Uncapped throughput for this window | 195.73 fields/s | 80.47 fields/s |
| Composition phase per presentation | 0.031 ms | 7.164 ms |

The paired median interval increase is 143.50%. The expensive phase is the
custom renderer, which replays the real Super FX core for private camera
views. TCP in the diagnostic gameplay run reported 1,036,151,067 private
replay instructions by field 1696, 1,595 camera passes, and zero failures.
The largest custom draw measured so far in that run was 17.105 ms. These
diagnostic numbers are attribution evidence, not production benchmarks.

Independent production runs with desktop presentation and real audio showed:

| Configuration | Mean interval | p95 | p99 | Maximum |
| --- | ---: | ---: | ---: | ---: |
| Stock | 17.88 ms | 19.28 ms | 20.14 ms | 21.97 ms |
| Fit + interpolated Auto | 19.17 ms | 30.35 ms | 40.43 ms | 47.25 ms |
| Fit + interpolated 60 | 18.53 ms | 27.36 ms | 30.79 ms | 36.49 ms |

These are one run per desktop configuration, with VSync explicitly disabled
in private configs. They establish host stutter on the route; they are not
five-pair desktop precision results, monitor scanout measurements, or a
measurement of the player's normal VSync/VRR settings. Auto versus 60 is not
a universal performance prescription. The actual desktop window can affect
Fit rendering cost. Individual enhancement isolation remains useful follow-up.

## Pacing overcount

Even stock has abundant uncapped headroom but presents at about 56 Hz in the
gameplay window. The mean `guest_periods` was approximately 1.0725 instead of
one. Over fields 1358–2102, guest master-clock snapshots advanced
265,817,778 clocks over 744 fields: **0.999759 actual master-clock periods per
field**. The discrepancy is therefore not explained by the console averaging
1.0725 real fields for each returned field.

`runner/src/apu_frame_clock.h:rtl_apu_clock_finish` clamps each iteration to at
least one field. Doom's beam driver explicitly allows CPU/DMA overshoot beyond
VBlank and carries the excess into the next field. That makes some CPU-clock
deltas long and the following deltas short. The minimum clamp retains the
long deltas but discards compensating short deltas. `RtlLastFramePeriods`
passes that duration to the host pacing clock. This is the concrete follow-up
for the extra slow pacing, and can also affect the APU timeline.

A fix must distinguish beam-owned elapsed field time from frame-model hosts
whose iterations legitimately cover multiple fields. It needs audio and
hardware-event comparisons as well as host interval validation; blindly
forcing every title's period to one would lose legitimate loading time.
No guest timing change was made in this investigation.

## Guest execution and original cadence

TCP `interp_stats` reported 100% interpreted CPU cycles, zero AOT cycles and
zero compiled dispatches in both title and gameplay samples. The generated
dispatch manifest contains only one null entry, rather than callable compiled
bodies. This is an interpreter-based port with HLE frame composition, not a
CPU AOT performance baseline. There were no reported native IRQ misses or
failed compiled dispatches; zero dispatches does not establish AOT coverage.
The original Super FX guest workload is also interpreted, separately from
private presentation replay.

In a 121-field movement capture (1480–1600), stock Snes9x had 35 whole-picture
changes and stock recomp had 33. Both visibly repeat pictures across several
console fields. Recomp renderer snapshots commonly span 5–7 fields, so smooth
presentation is separate from how often original game logic/render jobs
update. The two captures are workload/cadence evidence; differing field
boundaries and game state mean they are not a bit-exact oracle comparison.
Whole-picture changes can include the weapon and HUD, not just camera motion.

## Validation and reusable tools

The new developer TCP command `game presentation_stats` publishes copied,
thread-safe renderer counters, actual last submitted P1/P2 masks, and custom
draw timing. Successful queries, invalid-argument rejection, zero-input
snapshots, and a composed gameplay screenshot were exercised live.

The stock/enhanced correctness runs matched every-field WRAM/CPU trace and
all **84 guest snapshot files** (memory, registers and metadata). Thus the
presentation options preserved guest state on this route. These unpaced,
audio-disabled checks are not an audio oracle validation. Production's four
existing CTests passed. Both production and TCP Release builds compiled
successfully. The final production smoke run completed the route with a
5.124 ms mean uncapped interval; its binary SHA256 is
`42D329243F18818BE748A79136684E1F0BD0C7C87B1241B19CAFD5D8EC097B18`.

`tests/measure_lag.ps1` and `tests/LAG_DIAGNOSTICS.md` document how to repeat
the investigation. Earlier preliminary runs made during compilation, runs
with screenshots/state dumps, and TCP diagnostic runs were excluded from the
five-pair production performance results. Player settings and saves were
not changed by the isolated harness; test instances exit automatically.

The practical interim choice is to disable the display enhancements when
checking whether host stutter disappears. For further work, first correct and
validate beam/APU time accounting, then isolate interpolation versus extra
camera views, and profile private GSU replay. Expanding compiled CPU coverage
is a separate correctness-gated effort, not a generated-code hand edit.

Documentation consulted: the framework performance, frame-model, reference
frontend and Windows timing docs, plus the Retro Porting Toolkit's
[agent invariants](https://retroportingtoolkit.com/docs/agents/house-invariants),
[debug surfaces](https://retroportingtoolkit.com/docs/agents/machine-surfaces), and
[debugging guide](https://retroportingtoolkit.com/docs/guides/debug-a-divergence).

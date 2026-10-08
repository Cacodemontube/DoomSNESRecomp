# Lag fixes, 2026-10-08

The playable `build/DoomSNESRecomp.exe` includes the pacing and replay fixes below, plus the audio scheduling and weapon fixes described in final verification:

1. The shared beam driver selects beam-owned frame timing. Each returned field
   advances the host deadline by one period, while the APU carries actual CPU
   elapsed time across long/short DMA deltas. The existing minimum and
   multi-field loading behavior remain in place for other frame-model hosts.
2. Doom's private Super FX presentation passes omit unused instruction-history
   writes. They execute the same hardware instructions, clocks and PC hooks,
   preserve instruction counts, and retain the same bounded execution guard.
   Native Super FX history and the existing replay APIs retain their traces.

The input code, player settings, controller mapping and deadzone were not
changed. Validation scripts use isolated executable/config copies and perform
their own deliberate movement; normal launching does not use those scripts.

## Correctness

- Eight CTests passed, including new overshoot-repayment and replay-history
  equivalence regressions. The clock test also checks WAI/minimum behavior,
  multi-field loading, and a zero new CPU-clock delta.
- The replay regression checks architectural registers/cache/pixel buffers,
  private RAM, instruction counts and pipeline state, both with and without
  a redirecting PC hook. It checks source immutability, hook cleanup, alias
  rejection and the explicit diagnostic-history difference.
- Doom's renderer lifecycle test passed against the user-owned US ROM.
- The shared audio-delivery regression passed for priming, sample rates,
  starvation ramps, turbo recovery, producer isolation and console reset.
- The pacing correction preserves all 12 captured menu/gameplay framebuffer
  images. Some audio-polling/stack bytes and execution-clock metadata differ
  from the mistimed baseline, so this is not a claim of full guest-state
  equality with the previous timing bug.
- The rendering optimization alone matches all 96 snapshot files, 121
  presented images and every-field WRAM/CPU traces against the pacing-only
  build. Those captures include widened/interpolated presentation.

Artifacts remain private in ignored `build-lag-evidence/` directories.
`pacing-fixed-state`, `pacing-fixed-stock-paced`, `replay-traced-state` and
`replay-fast-state` retain the comparison inputs and logs. Capture/trace runs
are excluded from performance claims.

## Initial timing measurements

With real desktop presentation/audio and stock display settings, fields
1400–1900 now average **16.638 ms** between presents (about 60.10 Hz), compared
with **17.883 ms** before the fix. Mean `guest_periods` is exactly one. The p99
was 19.75 ms and maximum 20.46 ms; this is one desktop run per version with
VSync disabled in isolated configs, not a scanout or VRR measurement.

The initial rendering comparison reduced enhanced uncapped interval from
12.437 ms to 11.700 ms, and composition from 7.160 ms to 6.386 ms. Repeated
order-balanced results and live audio checks are recorded below after validation.

## Live audio

An optimized TCP developer build was run with real desktop audio and normal
pacing. Samples at fields 575, 763, 944, 1126, 1308, 1490 and 1671 showed zero
dropped samples and zero audible drops. Underflows remained at the startup
count of nine throughout that interval, with no new gameplay underflows.
Occupancy stayed between 1597 and 2279 native samples. The developer build
also averaged 16.634 ms per stock presentation; its additional instrumentation
makes it unsuitable as the production performance baseline.

This validates production/consumption telemetry on a local route, rather than
proving perceptual audio quality or equivalence to an external audio oracle.
The audio-delivery regression was compiled against the fetched SDL3 headers
because the standalone runner did not discover them automatically.

These fixes improve the measured causes; original low-rate game simulation
and expensive multi-camera replay can still limit smoothness. Compiled guest
CPU coverage was not expanded, and display enhancement selections were not
changed for the player.

## Final build verification

Final production SHA256:
79E0147A6467870A4D8F1F98A41A944741E70FDD87BD4FC688748E880992EC23.
Eight CTests and the ROM-backed renderer integration check passed.

The host now reserves time for the next audio-producing simulation step before
running optional interpolated presentations, and skips presentation when the
simulation is overdue. Optional timing CSVs include cumulative audio underflows,
missing frames, audible drops and queue occupancy.

Final paced desktop/audio routes with both display enhancements enabled:

| Mode | Gameplay underflows | Missing frames | Audible drops | Mean presentation interval |
| --- | --- | --- | --- | --- |
| Auto | 5 to 5 | 2665 to 2665 | 0 | 17.114 ms |
| 120 requested | 4 to 4 | 2132 to 2132 | 0 | 13.623 ms |

Counts include startup; neither final route added gameplay underruns between
fields approximately 1358 and 2102. Requested presentation rate is not a promise
of sustained output rate on this machine. Evidence: final-audio-auto and
final-audio-120 under build-lag-evidence. This validates local buffer delivery,
not all possible sources of perceptual crackle.

Weapon rendering now reconstructs the full native layout and respects each
tile's palette, restoring muzzle flashes without changing the approved bobbing
or opacity behavior. See WEAPON_INTERPOLATION.md for capture/state verification.

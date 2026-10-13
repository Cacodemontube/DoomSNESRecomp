# Review build: mouse pointer, automap projectiles and performance

This describes the previous review build. [The latest follow-up](V005_FOLLOWUP.md)
corrects its projectile type numbers, native iterator filtering and arrow
interpolation, and adds original-cadence vertical look. The old projectile
classification checks below did not catch the gameplay failure shown in MISS.mp4.

Built for local review in `build-review-release`, not published. Previous executable,
assets, mod catalog/state and configuration are preserved in
`build-review-release/rollback-20261012-213433`. Exit the application before restoring
that copy. Current personal config, keybinds, saves and ROM selection were preserved.

## Changes

- Modern controls has a **Mouse skull pointer** choice, default **On**. The native
  animated skull follows the pointer; hover selects the native item and left click
  confirms it. Keyboard arrows still work. The cursor is mapped through the selected
  display aspect/letterboxing, with the system cursor hidden while the skull is active.
  Turning the option off restores relative mouse menu navigation. Works with display
  enhancements enabled or disabled. Gameplay still uses relative mouse capture.
- The second IDDT mode includes active native fireball, rocket and plasma object types
  65 through 69. Barrel explosions, unrelated objects and dead enemies stay excluded.
  This applies to both native and enhanced automap drawing.
- The strip in GL.png lies in native row 22, immediately above the world starting at
  row 23. Enhanced-world composition now clears that single border row instead of
  allowing native scratch pixels to show through. Ordinary native composition is
  preserved. The production border policy has a regression check; replaying the video
  is not a proof that every random occurrence has been reproduced.
- Nearest-neighbor expansion now expands each source row once and copies its repeated
  output rows. It avoids coordinate division for every output pixel. Padded buffers,
  scales 2 through 4 and widescreen widths are covered by existing pixel checks.
  Simulation speed, collision, native game logic, AI, RNG and audio timing were not
  changed by this optimization.

## Measurements

Diagnostic Release builds, user-owned US ROM, deterministic E1M1 movement/fire/turn
route, 4x resolution, 16:9, requested 144 FPS, unpaced, 501 measured samples:

| Measurement | Before | After |
| --- | ---: | ---: |
| Mean interval | 23.35 ms | 21.58 ms |
| Mean composition | 14.64 ms | 12.88 ms |
| 95th percentile interval | 32.01 ms | 30.27 ms |

This is about 8% less total work on that route. The complete native state trace and
frame-1500 captured image are byte-identical. Evidence is in
`build-lag-evidence/review004-before` and `review004-after`. It is not a guaranteed
8% FPS increase on every system or scene.

A more aggressive render-cost scheduling experiment reduced picture frequency
without improving simulation cadence; it was removed. Final pacing retains the
existing simulation-priority/catch-up policy. The paced diagnostic run with audio
held about 60.07 native updates/second over fields 1400 through 1900. No audio
underflows, missing frames or dropped audible samples were recorded in that segment.
The stock stationary-camera route averaged 16.64 ms per presentation (about 60 Hz).
The normal, diagnostics-disabled review executable also completed the enhanced
movement/pause/resume route with audio enabled.

## The Linux report and remaining limits

The reported braking sensation is not conclusively fixed. This change reduces render
work; it does not prove the intermittent symptom is gone on the user's machine.
Likewise, the Linux music stutter has not been reproduced on the reported AMD laptop.
There is no evidence from these tests of an AMD-specific GPU problem. Most expensive
work here runs on the CPU: guest simulation, private Super FX scene replays and world
rasterization. Low total CPU utilization can hide a thread missing its frame deadline.
Standing against a wall does not stop guest execution or audio generation.

Diagnostic timing separates guest work, rasterization, composition, presentation,
waits and audio queue failures. SPC/DSP guest synchronization took about 0.565 seconds
across the entire paced cold-boot gameplay route on this machine, suggesting that
sound execution is not its dominant cost. This is evidence for this Windows setup,
not a measurement of the Linux reporter's setup. Existing EnableAudio=0 disables host
sound output; SPC execution remains because the ROM's sound-loader handshakes depend
on it. A mute control cannot safely be described as skipping all native sound code.

Without interpolation the original SNES world updates less frequently than the
60 Hz display. That deliberate cadence differs from audio crackle or simulation
slowdown. Resolution and interpolation increase CPU work even when they make motion
look smoother. Further speedups are possible, but replacing the native visibility,
texture/lighting or game simulation code requires additional equivalence testing.
No approximation, reduced update rate or altered game physics was introduced here.

## Validation

- All 15 CTests passed in diagnostic and normal Release configurations.
- ROM-backed renderer lifecycle test passed in both configurations.
- Input tests cover native menu selection, ignored clicks outside menu items,
  pointer disablement, keyboard/mouse controls, crouching and focus behavior.
- Live enhanced menu tests verify native hover selection and the animated skull in
  a black border region, so background/menu lettering cannot satisfy the pixel check.
- Live native-display menu tests verify the pointer without a captured enhanced world.
- Projectile tests cover all five native missile/fireball/plasma tags, zero monster
  health on projectiles, and exclusion of barrel explosions.
- Display mapping and border tests cover letterboxing and non-finite coordinates.

Reproducible scripts: `tests/measure_lag.ps1` and
`tests/validate_menu_pointer.ps1` (use `-NativeDisplay` to test original presentation).
Detailed review evidence stays local under `build-lag-evidence` and `build-evidence`.

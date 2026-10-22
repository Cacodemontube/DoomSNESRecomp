# Interpolation optimization, 2026-10-14

The current source has two additional optimizations:

- Native 8bpp framebuffer decoding processes eight pixels together, loading
  each plane once per group. It preserves pixel order, RAM wrapping and strip
  boundaries, and works without host-endian or alignment assumptions.
- OpenGL shader presets reuse intermediate texture storage until its dimensions
  change. Previously every intermediate pass called `glTexImage2D` on every
  presentation. This change affects multi-pass presets; the default SDL
  renderer and single-pass presets do not use intermediate shader textures.

The test build is `build-interpolation-release/DoomSNESRecomp.exe`, a Windows
x64 Release build with HLE composition and developer tracing disabled. It uses
the existing private generated game code. The normal `build/` runtime was not
replaced. Select the presentation features in the launcher's Mods page when
trying this separate build.

## Evidence and limits

Eight relevant CTests passed, along with the ROM-backed renderer-state test
and the real-driver `glsl_storage_probe`. The latter checked 125 consecutive
frames in each of RGBA, float and sRGB modes, read back the actual rendered
pixels, and verified that only three storage allocations occur: initialization,
source resize and a change to viewport-sized output. It checked that a viewport
resize does not reallocate a source-sized pass.

All 278 gameplay capture/state files and the complete per-field guest trace
matched before and after the changes. This route used widened interpolation,
2x resolution, pause/resume and a two-pass OpenGL preset. These capture files
are the composed host images, before shaders; shader pixel correctness is
covered separately by the real-driver probe.

The order-alternated decoder benchmark measured medians of 378 ms for the
original decoder and 80 ms for the new decoder over 2,000 three-strip frames
(about 4.7x throughput for decoding alone). Run it with:

```powershell
./build-interpolation-release/doom_renderer_state_test.exe --benchmark-decode
```

Whole-game uncapped runs were noisy: both guest and composition timings moved
between runs. They do not establish a reliable whole-game percentage gain.
Capture runs deliberately add file IO and are excluded from speed claims.

On the local RTX 2060 SUPER, live interpolation-only runs at requested
60/120/360 FPS with Direct3D 11 and 120 FPS with OpenGL maintained approximately
60.04–60.11 guest fields per second, with zero new audio underflows, missing
samples or audible drops in the measured gameplay window. Presented rates
were approximately 55/72/93 FPS for Direct3D 11 and 77 FPS for OpenGL. A requested
FPS is a cap, not a guarantee. The CPU executes additional private Doom render
passes for interpolated images; resolution and widescreen also add CPU work.

These measurements do **not** prove or rule out an RX 5700 XT driver issue.
The affected PC must be measured before claiming that its lag is fixed.

## Comparing the affected PC

The harness now explicitly selects Renderer as well as OutputMethod, preventing
an inherited Renderer setting from overriding a test. It reports actual
simulation speed separately from presented FPS, renderer identity, phase
timings and changes in audio starvation counters. OpenGL profiling additionally
records vendor, GPU model and driver version.

Use the playable game directory, after ROM setup/generation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/compare_renderers.ps1 -BinaryDirectory "C:/Games/DoomSNESRecomp" -Rom "C:/Roms/Doom (USA).sfc" -IncludeVSync
```

This compares Direct3D 11 at 60, 120 and 360 FPS, OpenGL at 120 FPS, and
optionally stock mode with Direct3D 11 and VSync off/on. High-frame-rate
interpolation deliberately forces effective driver VSync off, even when its
config setting is on; SDL summaries expose `vsyncEffective` separately from
the requested `vsync`. Each test runs a fixed route in
an isolated directory and deliberately walks, turns, fires and pauses. Existing
saves and settings stay in the installed game directory. The result zip contains
only timings, summaries and logs; no ROM, generated code or saves.

The default isolates interpolation at native resolution. Add `-Enhancements`
for widescreen, and `-ResolutionScale 4` (or 2/3) to reproduce that additional
setting. Use the same arguments on both PCs. Low guest-field rate indicates
slow simulation; high `compose_ms` indicates CPU presentation work; high
`present_ms` includes driver/upload/swap blocking and VSync, so it alone does
not establish a GPU incompatibility. Desktop tests still do not measure monitor
scanout or perceptual input latency.

## Rollback

The ignored backup directory is
`build-lag-evidence/optimization-rollback-20261014/`. It contains original
source copies and the original Release executable in `runtime/`. Git rollback
branches with the name `rollback/before-interpolation-optimization-20261014`
exist in both the game repository and its framework submodule. The pre-existing
framework clock-test change was preserved and is outside this rollback.

Preview and validate rollback, then apply it:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build-lag-evidence/optimization-rollback-20261014/rollback.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File build-lag-evidence/optimization-rollback-20261014/rollback.ps1 -Apply
```

The script checks every target and backup before restoring any file. It refuses
to overwrite subsequent edits. It restores only this optimization's files and
removes its new tests/docs; it does not reset the repository or change player
settings. Rebuild `build-interpolation-release` after restoring sources, or use
the separately retained original runtime immediately. Keep the backup directory
while evaluating the changes.

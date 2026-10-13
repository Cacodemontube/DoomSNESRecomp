# v0.02 performance and controls follow-up

This describes the first follow-up. The later changes in
[controls, transparent automap and retained noclip rendering](V003_FOLLOWUP.md)
supersede its always-on crouching and black-void behavior.

The current source adds Ctrl crouching, keyboard/wheel automap zoom, run-ahead
input-state restoration, and fixes empty-camera fallback in enhanced noclip
rendering. This work has not been published as a new release.

## Controls

Hold either Ctrl key to crouch with modern controls. The native eye drops from
42 to 21 units and passage/moving-ceiling checks use a 28-unit body instead of
56. Releasing Ctrl under insufficient clearance retains the crouch until there
is space to stand. WASD still works; Shift running is suppressed while holding
Ctrl. Native aiming, enemy targeting, player artwork and ordinary walking speed
remain the SNES implementation; this is not a wholesale GZDoom physics port.
Crouching does not require the vertical-look option. Its four extra GSU observers
are installed only while needed. Ctrl+L/Ctrl+R host shortcuts are retained.

With the map open, +/= and keypad + zoom in; - and keypad - zoom out. Opposing
keys cancel. Mouse-wheel up/down queues native zoom ticks; flipped SDL wheel
direction is honored. Requests are consumed by native player updates, rather
than presentation polls. They are bounded and cleared on focus loss, suspension
and map exit. The original modern-controls mask removed the native X/weapon
bit even on the map, where that bit means zoom out; both suppression sites now
apply only outside the map. The game retains its own zoom limits.

## Run-ahead

Run-ahead performs a real field, captures its raster side effects, snapshots,
simulates N more fields, captures the predicted picture, and restores the real
timeline. It adds substantial CPU work. The host input adapter's jump height,
velocity, field counters, mouse accumulator, weapon request and queued map zoom
previously survived speculation without being restored. These now use the
framework's rollback execution-state payload. Speculative fields also advance
the jump's field clock. SDL focus/capture and configuration remain host policy.

Developer test requests previously applied from the predicted frame's capture,
which could consume a weapon/jump request immediately before rollback erased
the guest change. They now apply on the emulation thread before real simulation.

Start with run-ahead **off**, widescreen off, 2x resolution and 60 interpolated FPS on machines
that struggle. One-frame run-ahead is useful only when there is CPU headroom;
increasing it cannot improve an overloaded frame budget. Interpolation smooths
presentation while preserving the SNES game's slower, discrete simulation.

## Rendering and measured cost

An empty center or wing camera no longer invalidates the enhanced raster or
skips its columns. High-resolution rays use the merged geometry even if their
discovery camera is empty. All uncovered rays start with black void. Native
hall-of-mirrors pixels are stale framebuffer contents, not additional geometry
that can be rerendered at higher resolution. Void can therefore still be black
on one side; the enhanced viewport remains the selected width and resolution.

The renderer now caches the world image between unchanged replays, checking
width, scale, pitch/horizon, beam palettes and visible rows. It copies only the
world rectangle, leaving HUD, weapon, messages and automap composition live.
Replay/reset invalidates the cache. High-resolution geometry also skips native
wall texture-phase matching that its world-UV raster never consumes.

Measurements below are single local Windows runs on an i7-10700, optimized
GCC builds with trace support, dummy SDL display, VSync off and unpaced scripted
routes. They measure service cost, not achieved 144 Hz. Muted runs still execute
the guest SPC/DSP. Captured idle runs include one synchronous screenshot, so their
maximum/p99 stalls should not be used as ordinary-play guarantees.

| Route/settings | World cache | Mean interval | Mean composition |
| --- | --- | ---: | ---: |
| Moving, 4x/16:9/interpolation | disabled | 28.92 ms | 20.90 ms |
| Same moving route/settings | enabled | 28.13 ms | 20.19 ms |
| Stationary, 4x/16:9, no interpolation | disabled | 18.08 ms | 9.39 ms |
| Same stationary route/settings | enabled | 17.45 ms | 8.62 ms |

Each pair uses the same executable. The stationary composed captures have
identical SHA-256 hashes. Gains are modest, especially during motion; this does
not fix every reported performance problem. CPU world composition dominates
the moving enhanced route. Its guest service is roughly 6.2 ms/field, versus
about 0.76 ms/field measured inside SPC/DSP guest-sync batches. That audio timing
is a subset of guest time and includes instrumentation overhead; it does not
measure every loader path or establish the cause of a particular audible click.

Stock run-ahead 0 versus 1 measured 8.34 versus 14.56 ms mean interval. The
restored CPU/RAM state traces match exactly through the route. These timing runs
use different intermediate executable hashes and are an illustrative cost check,
not a same-binary performance regression comparison.

## Validation and evidence

- Optimized Windows x64 builds with trace on and off both compile and pass
  all 15 CTest regressions, plus the ROM-backed renderer lifecycle test.
- The SDL adapter test covers Ctrl movement, half-height eye/clearance, moving
  ceilings without affecting other objects, low-ceiling release, selective hook
  registration, keyboard zoom, flipped wheel input, queue consumption and rollback.
  A crouched low-ceiling state also recovers after host input reset on state load.
- Live modern controls checks at 4x/16:9 and one-frame run-ahead at 2x/16:9 verify
  native weapon changes, jump/landing, 21-unit crouched eye and standing recovery,
  map zoom, concurrent strafe/mouse turn, and vertical look with no replay failures.
- A 4x/16:9 IDCLIP route captures eight out-of-bounds directions, including zero
  central segments, without replay failures or mixed native-resolution fallback.
- Twelve chainsaw/pause cycles, held menus and submenus pass pixel checks.
- Paced dummy-device E1M1 playback adds no output underflows or audible drops
  between fields 1400 and 2000; BRR decoding matches the in-process reference.

Artifacts are under `build-lag-evidence/review002-*`; build logs and private
ROM-derived generated input are under `build-evidence/review-002`. No ROM or
generated code is committed. `DOOM_GENERATED_DIR` permits a separate generated
directory because writing the workspace's existing `src/gen` was denied.

Repeat measurements with `tests/measure_lag.ps1`, optionally `-RunAhead 1` or
`-DisableWorldCache`. `-RouteFile tests/presentation_idle_route.txt` selects the
stationary route. `SNESRECOMP_FRAME_TIMING` writes buffered stage timing at exit;
`SNESRECOMP_HOST_PROFILE=1` reports stages, and `SNESRECOMP_PHASE_MS=1` adds audio
batch timing. Profiling remains optional and adds overhead.

The local build with trace off is `build-review-release/DoomSNESRecomp.exe`,
with its staged `assets/` and `mods/`. Source changes also exist in the
`snesrecomp` submodule (audio profiling); they must accompany the parent changes
when committing or preparing a release.

A paced smoke run of that build at 2x/16:9/60 requested FPS completed normally,
but measured 19.57 ms mean presentation interval (about 51 presentations/s) on
the movement window. Even 2x with widescreen should not be assumed to sustain
60 FPS on every machine. The corresponding 2x/4:3 run measured 18.86 ms
(about 53 presentations/s). These instrumented runs still miss some deadlines;
reduce to native resolution as well if widescreen-off/2x remains too slow.

The reported Ryzen/Linux and i7-12700/Radeon systems were unavailable. Linux,
macOS and 32-bit builds have not been exercised in this follow-up. The reported
music crackle/stutter remains unresolved; the dummy-device delivery check does
not replace listening and testing on affected hardware. A complete SPC bypass
was not added: the guest uses sound-processor handshakes during startup/loading,
and skipping them is not equivalent to muting output.

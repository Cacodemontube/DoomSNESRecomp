# Controls, translucent automap and rendering follow-up

## Changes

**Crouching** is now an option under Modern keyboard + mouse and defaults to
Off, like Jumping. Enable it to use hold-Ctrl crouching. Missing/old saved option
values also leave it disabled. Turning it off removes the additional crouch
observers and clears host crouch requests.

The Ctrl+mouse-1 halt came from the desktop window hit-test: Ctrl marked the
whole window draggable. The OS can then consume the click and enter its modal
window-move loop before the game receives mouse input. The hit-test now returns
normal mouse handling while relative mouse capture is active, including at
resize edges. Releasing capture restores the ordinary window dragging behavior.
This follows [SDL's documented hit-test behavior](https://wiki.libsdl.org/SDL3/SDL_SetWindowHitTest).
The SDL adapter regression checks crouch plus fire together; the OS drag loop
itself was not exercised by physical input in the headless check.

The translucent map's background was losing its interpolation endpoints. Both
the BSP entry and the first map strip captured the world in the same field.
The second capture saw a zero interval and cleared the previous world state.
The map maintained its separate history, explaining why it stayed smooth.
Duplicate same-field map-world captures are now ignored. This preserves 3D
camera/scene interpolation without changing native simulation timing. The
Interpolated frame rate option must be enabled for both layers to interpolate;
CPU overload can still reduce presentation rate, and enemy artwork retains
the original game's discrete animation frames.

Noclip once again retains pixels in areas where no valid geometry overwrites
them, recreating the original trailing-image/hall-of-mirrors effect. At higher
resolution those pixels come from the previous enhanced world image, not from
an enlarged SNES framebuffer. Empty center/side discovery cameras remain valid.
Reset, changed width/scale and newly visible rows without history start black
until a high-resolution image exists to retain. Cached history excludes HUD,
weapon, menu tint, map and message overlays.

Per-pixel wall/sprite/sky sampling now performs bounded signed integer flooring
directly, including negative coordinates. Plane dither parity and sky column
coordinates are computed once per column. These replace repeated library math
calls without changing sample selection, geometry or guest state.

## GPU question and CPU cost

The two AMD reports do not establish an AMD-specific fault. The current world
renderer is CPU based: native guest work plus isolated Super FX visibility/build
and draw-stage replays, followed by CPU ray projection, wall/plane texture sampling,
lighting, depth tests and sprites. Widescreen adds two discovery cameras.
Interpolation requests new presentation poses between native updates, so it can
repeat that work at the requested display rate. The graphics backend then
uploads/presents the composed image; changing GPU vendor does not move this CPU
renderer onto the GPU. Driver/backend differences remain possible and have not
been tested on the reported AMD systems.

At 4x/16:9 the world is roughly 1208x576, about 696,000 pixels. At 144 Hz that
is approximately 100 million world-pixel positions per second before overlapping
surfaces/sprites, replay work and composition. Replay counters from the controls
route record hundreds of millions of interpreted Super FX instructions over the
test. This explains why overall multicore CPU utilization can look modest while
the serial guest/presentation path misses a deadline.

Further optimization can preserve authenticity. Caching, exact integer math,
faster equivalent Super FX execution, and moving the host raster to GPU shaders
can leave native game logic, tick rate, collision, cheats, audio and random state
unchanged. A GPU rasterizer would need palette/dither/texture/depth comparisons
and testing across vendors and operating systems. It is a larger renderer change
and has not been implemented in this follow-up. Increasing simulation speed or
inventing enemy animation frames would change gameplay/presentation behavior;
neither was done.

## Validation

- All 15 CTest regressions plus the ROM-backed renderer lifecycle test pass.
  Both diagnostic and normal optimized Windows builds pass.
- Input tests cover off-by-default crouching, enabling/disabling it, Ctrl+fire,
  low-ceiling standing prevention and selective observer registration.
- The lifecycle test reproduces the double capture and verifies previous camera
  state/interval survive. It also verifies enhanced image retention, row masking,
  palette/horizon changes and reset behavior.
- The live 2x/16:9 translucent/interpolated map check records both map interpolation
  and 3D-background interpolation (25 then 41 background presentations) with no
  replay failures. Live modern controls, weapons, jumping, crouch, zoom and look pass.
- The math regression compares flooring and adjacent floating-point values across
  more than 260,000 signed coordinates against the standard floor function.
- The before/after 4x/16:9 movement captures are byte-identical (SHA-256
  `F62665D8DABF36F7BD2FE9E7D1B405D5751BC422AF7BB043C8215D416F13E5C7`).
  Mean composition measured 19.64 versus 19.40 ms, with mean interval 27.94 versus
  27.78 ms. This small difference is within normal single-run variability; it is
  not evidence of a large FPS improvement. Each unpaced, dummy-display run includes
  one synchronous capture; its maximum stall should not be treated as normal play.
- The live 4x/16:9 noclip check captures eight directions with no replay
  failures. Uncovered views retain prior enhanced imagery instead of black void.

Evidence is under `build-lag-evidence/review003-*`. Updated local binaries are in
`build-lag-debug` and `build-review-release`; no new release has been published.
The host hit-test and existing audio profiling changes live in the `snesrecomp`
submodule and must accompany the parent repository changes when committing.

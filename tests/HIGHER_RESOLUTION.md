# Higher 3D resolution

In Mods > Doom Display Enhancements, enable Higher 3D resolution and select
2x, 3x or 4x. Start with 2x. At 4:3 the world viewport is respectively
432x288, 648x432 or 864x576. Widescreen extends the world horizontally.
The largest supported composition is 2728x896 at 32:9 and 4x.

This feature works independently of widescreen and frame interpolation.
Disabling it restores the existing native world renderer. The original HUD
and weapon artwork remain at their original logical size. Existing weapon
opacity, muzzle flashes and interpolation settings are retained.

## Implementation

Private Super FX rendering jobs produce native visible-segment, wall-plot
and object records. The host rasterizer samples original wall and object
artwork directly at the selected resolution rather than enlarging the
already rasterized world. Wall placement uses immutable map vertices and
native BUILD texture offsets and vertical origins, including moving doors.
This avoids using rounded screen samples as texture anchors. Native
RSPDistance is twice map distance, so the wall sampler's half-scale multiply
corresponds to one texture column per map unit, regardless of texture width.
Aspect correction affects projection rather than texture density.

Wall clipping follows the native BUILD wall, upper/lower clip and plane
flags and its resolved heights. Sky portals leave the upper opening intact
so taller walls behind them are not cropped at a nearer sector's sky height.
Map vertices are projected continuously instead of reusing the GSU's
integer rotation cache, keeping shared wall endpoints consistent across
the widescreen visibility cameras.

Object rendering uses the native visible-object list, original compressed
image columns, palette shading and flip flags. Per-pixel depth tests hide
objects behind walls. Distant objects use four artwork samples per output
pixel when minifying, including transparent coverage, to preserve details.
This improves sampling of existing assets; it does not introduce new HD art.

The guest simulation, input and sound engine are unchanged. Rendering reads
private cartridge RAM snapshots and immutable ROM data. Unsupported scenes
fall back to the centered native picture. Native texture and object formats
were checked against the original DOOM-FX rendering source.

## Validation

The CTest suite covers resolution selection, texture and sprite
decoding, native texture-phase wrap in both directions, sprite occlusion,
padded pitches and guards around the world/HUD boundaries at 2x through 4x.
The renderer state test also checks lifecycle behavior with the local US ROM.

Geometry regressions check biased ROM vertex addressing, matching endpoints
in three cameras, texture placement through camera movement, sky portals
with taller walls behind them, and real upper clips. The isolated gameplay
harness `tests/validate_render_geometry.ps1` captures E3M1, E3M2, E3M3, E3M8
and E1M1 while walking, turning and opening doors. Use `-Scale 2` or `-Scale 4`
for enhanced captures, or `-Native` for the original renderer reference.
Evidence from this correction is in `geometry-original`, `geometry-before`,
`geometry-final-2x` and `geometry-final-4x` under `build-lag-evidence`.

The isolated gameplay route at 2x produced byte-identical CPU state traces
and all 156 native dump artifacts compared with the existing replay baseline.
This includes cartridge RAM, CPU/Super FX, audio and native framebuffer dumps.
Composed screenshots differ intentionally because the world is rerendered.

Local evidence is under build-lag-evidence:

- resolution-native-state-final: native-state equivalence, current release build.
- resolution-objects-2x-corrected and resolution-final-4x: object-detail captures.
- resolution-final-audio: paced desktop/audio route; gameplay underflows and
  missing samples did not increase, and audible drops remained zero.
- resolution-tcp-verified: TCP reported a supported 4x scene with 32 segments, zero
  replay failures and a successful composed screenshot at 2728x896.
- resolution-wide-final and resolution-ultrawide-final: explicit 16:9 and
  32:9 captures rather than relying on the window's aspect ratio.

The desktop/audio 2x run averaged an 18.32 ms presentation interval on this
machine. Higher resolutions and wider views cost additional CPU time;
selected presentation rates remain targets rather than guarantees.
Screenshot runs include capture overhead and are not frame-rate benchmarks.

The 4x corner-stability correction combines the three native visibility
lists into one central-camera geometry list. This retains thin walls that
the native two-pixel occlusion test can omit and removes camera-boundary
changes in wall clipping. Exact ROM endpoints, texture density, offsets
and height anchors are preserved. Conservative angular bins reduce ray
intersection work without changing the resulting hits; tests compare them
with exhaustive intersections, including near-eye and behind-eye segments.

High-resolution rendering also skips the redundant private native world
DRAW stages and low-resolution composition. Native BUILD, face animation
and message stages still run on private RAM. Setting the developer-only
environment variable `DOOM_HIGHRES_NATIVE_DRAW=1` restores those DRAW stages
for comparison. All eight E3M1/E3M8 captures (start, forward, open and turn)
were byte-identical with this oracle. The normal low-resolution path is
unchanged. The 4x state route also produced byte-identical CPU traces and
all 156 native dump artifacts against `resolution-native-state-final`;
evidence is in `geometry-stable-native-state-4x`.

The paced 4x, 16:9 walking route with a 60 FPS target averaged 39.53 ms
between presentations before this correction and 34.42 ms afterward on
this machine, about 25 to 29 FPS. Its 95th-percentile interval fell from
71.98 to 62.29 ms. These dummy-video, audio-disabled runs are evidence of
reduced cost, not a guarantee of 60 FPS or a desktop/audio benchmark.
Evidence is in `geometry-walk-baseline-4x`, `geometry-walk-fast-4x`,
`geometry-fast-4x`, `geometry-full-oracle-4x` and
`geometry-merged-final-4x` under `build-lag-evidence`.

Repeat a controlled run with:

    powershell -NoProfile -ExecutionPolicy Bypass -File tests/measure_lag.ps1 -BinaryDirectory ./build -RunLabel my-resolution-run -ResolutionScale 2 -Enhancements -Aspect 16:9 -Capture

The harness creates an isolated executable/configuration and drives its own
scripted input. It does not change the player's settings or drive a running
personal game. Developer TCP composition/screenshot buffers accommodate the
higher output dimensions. Native state dumps retain native dimensions.

## Further corner recovery and menus

High-resolution geometry also includes front-facing ROM map edges omitted
by every native visibility camera. The USA WALLS and alternate-ID table
addresses are checked against their GSU instructions before decoding.
Current private sector heights supply moving-door and portal clipping;
texture offsets, density and pegging keep the existing world alignment.
This recovery works at both 4:3 and widescreen. Ray sorting discards hits
behind the nearest solid wall, with an exhaustive-prefix oracle test.

Native menu IRQ phases retain the level snapshot beyond the normal history
timeout. Scanline color math supplies the original white tint throughout
the extended viewport. Mode 3 BG2 and visible OBJ extraction preserve
centered native lettering and the skull cursor over the frozen high-resolution
background. Weapon artwork remains frozen, and fades still follow native
scanout. Presentation dimensions continue using the selected scale.

`geometry-complete5-4x` captures E3M1, E3M8, E1M1, E3M2 and E3M3,
including long menu holds, at 4x with interpolation. All replay failure
counters remained zero. `geometry-normal-final-4x` checks 4:3, and
`automap-geometry-final-4x` checks transparent, interpolated automap and
returning to gameplay. `geometry-release-native-state-4x` matches
`resolution-native-state-final` byte for byte in its CPU trace and all
156 native dump artifacts. These checks do not prove that every possible
viewpoint is free of visual artifacts. The release preview uses
`tests/presentation_menu_route.txt`, holding the native menu for 300 fields;
`menu-release-preview2-4x` captures its presented frame at field 1900,
including the retained weapon and complete status bar across menu setup.

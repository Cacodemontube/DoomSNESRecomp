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
already rasterized world. Native wall plots provide texture columns and
vertical phases. Interpolation uses reciprocal depth and unwraps repeating
texture coordinates before blending, preserving native texture placement
and avoiding stretching across a texture's wrap boundary.

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

All nine CTest tests pass, including resolution selection, texture and sprite
decoding, native texture-phase wrap in both directions, sprite occlusion,
padded pitches and guards around the world/HUD boundaries at 2x through 4x.
The renderer state test also passes with the local US ROM.

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

Repeat a controlled run with:

    powershell -NoProfile -ExecutionPolicy Bypass -File tests/measure_lag.ps1 -BinaryDirectory ./build -RunLabel my-resolution-run -ResolutionScale 2 -Enhancements -Aspect 16:9 -Capture

The harness creates an isolated executable/configuration and drives its own
scripted input. It does not change the player's settings or drive a running
personal game. Developer TCP composition/screenshot buffers accommodate the
higher output dimensions. Native state dumps retain native dimensions.

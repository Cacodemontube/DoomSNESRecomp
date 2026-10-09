# Automap presentation

Higher 3D resolution also redraws native automap lines and arrows at 2x, 3x
or 4x. Interpolated frame rate smooths map translation, rotation and zoom,
including at native output size. Transparent automap is a separate,
disabled-by-default Presentation toggle in Doom Display Enhancements.
It renders a live private 3D view behind the map with 25% black dimming.
The status bar and map label retain native pixel artwork. The native map
controls and their behavior are unchanged.

The renderer observes the retail USA map's first drawing strip at E9D7,
captures absolute line endpoints and selected colors at EB41, collects
level-label text at the eight E5AD..E5C2 plot sites, and publishes the complete
map at E549. Geometry selection remains native: explored walls, hidden and
secret lines, Computer Map gray lines, and IDDT monster triangles all follow
the game's existing decisions. The other two strips provide the remaining
label text; their repeated geometry is not collected again.

Continuous map projection uses native ViewX/Y/Angle and AutoMapScale at
RAM 0068. Angle interpolation takes the short path across wrapping. Each
new map interval begins at the last displayed pose. Green arrow endpoints
are interpolated when matching bounded previous endpoints are available;
new or discontinuous arrows appear at their native position. Lines are
clipped before antialiased rasterization, and scanline visibility/palettes
respect native fades and protect the HUD. Disabled enhancements retain the
native automap.

Map observers are installed only while the native map is open, to avoid
adding instruction-hook lookup cost during normal 3D play. For transparency,
the renderer captures the map's scene and starts the private replay at BSP,
with AutoMap cleared only in private RAM. No emulated memory, registers,
input, map discovery, timing or gameplay is changed. Unsupported scenes
keep the native map until a valid enhanced frame is available.

## Checks

`doom_automap` tests continuous projection, zoom/angle interpolation, distant
line clipping, subpixel strokes, opacity, padded pitches, hidden scanlines
and world/HUD guards at all four scales and both centered/wider output.
The renderer state test checks native map-line collection, private snapshots,
palette observation while the weapon is hidden, and close/reopen lifecycle.

The isolated `validate_render_geometry.ps1` harness accepts `-Automap`,
`-Transparent` and `-Interpolate`, with `-Scale 1` for native-size output.
It captures movement, turning, zoom, IDDT monster mode and return to 3D.
For example:

    powershell -NoProfile -ExecutionPolicy Bypass -File tests/validate_render_geometry.ps1 -RunLabel my-map-check -Scale 4 -Automap -Transparent -Interpolate -Warps 11

Evidence is under `build-lag-evidence/automap-opaque-final-4x`,
`automap-overlay-final-4x` and `automap-interpolated-native-size`.
`automap-transparent-only` checks transparency with the other display
enhancements disabled. The final release executable also completed the
paced 4x/60 FPS map route with a composed capture in `automap-release-preview`.
The dedicated `presentation_automap_route.txt` includes map movement, zoom,
pause/resume and closing. Its 4x/widescreen/interpolated/transparent run
matched the native run's CPU state trace and all 117 native dump artifacts
byte for byte (`automap-native-state` and `automap-overlay-state-4x`).
These runs use isolated settings and never modify the player's mod selections.

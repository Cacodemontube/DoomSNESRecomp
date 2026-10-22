# Widescreen geometry repair — October 22, 2026

Inspected `Bug1.mp4`, including the E1M1 doorway/wall views and slow turns.
The native-resolution widescreen path still used separate visibility-camera
geometry when PC enemy rotations were disabled. Higher resolution and native
rotations already used merged geometry and recovered map edges. Changing
which visibility camera supplied a ray could therefore expose geometry omitted
by that camera's native two-pixel occlusion test.

Widescreen now uses the existing merged scene, edge recovery, ray bins,
prepared planes, parallel raster and world cache at native resolution too.
Native central sprite preservation and paired-column density remain intact.
The three raster ranges are aligned to whole two-pixel groups so their ray
samples and floor-dither phases agree at worker joins.

No asynchronous presentation/recovery code, acceptance checks, native game
execution, rotation artwork or infighting logic was changed. Non-widescreen
rendering keeps its existing selection of the fast presentation path.

## Validation

- Added a renderer regression in which the central camera sees a wall that
  the left native visibility camera omits. The old renderer fails the floor
  continuity assertion; the fixed renderer passes.
- Checks all logical widths 257–684 for ordered, complete, pair-aligned worker
  ranges. One-pass and three-part native widescreen raster pixels match.
- Renderer state suite and six targeted CTest checks pass: resolution,
  presentation, video, projection, shading and crosshair.
- Same scripted native-resolution widescreen gameplay/turn route completed
  1,673 fields in both builds, with zero replay failures. Captures were retained
  for visual inspection. This one local unpaced/no-audio run took 12.400 seconds
  before and 9.455 seconds after; it is a correctness/throughput check, not a
  promised FPS result for the friend's PC.

Normal development executable updated in `build/DoomSNESRecomp.exe`.
Prior normal/diagnostic executables and source copies are retained under
`build-evidence/widescreen-20261022/before/`.

The diagnostic-only friend package is
`dist/DoomSNESRecomp-Widescreen-Fix-Report-20261022-Windows-x64.zip`.
It contains one x64 game executable, runtime assets/mods/licenses and clean
settings. No editor/converter, ROM, WAD, saved game or personal resource path
is included. Widescreen 16:9, native resolution and 120 FPS interpolation
are selected so the geometry path is exercised immediately. Rotations remain
optional and need the player's own full Doom/Ultimate Doom WAD.

Freshly extracted package smoke checks exercise both widescreen and the
non-widescreen asynchronous path, check renderer health and validate the four
report ZIP entries against the original report files. Detailed results and
hashes are retained in `build-evidence/widescreen-20261022/`.

The friend's affected view/hardware still needs the supplied build retested;
these checks cannot establish the absence of every possible map-specific edge.

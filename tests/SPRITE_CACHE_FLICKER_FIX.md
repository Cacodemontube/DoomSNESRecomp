# Widescreen sprite cache flicker — October 22, 2026

Inspected `Bug2.mp4`, including the launcher/aspect change and widescreen
gameplay. The preceding widescreen geometry repair enabled the full world
cache for native-resolution widescreen without enemy rotations. That exposed
an early-return bug: cache hits copied the world and returned before composing
the original central sprite pixels. Those sprites are deliberately excluded
from the cached world. They therefore appeared on cache misses and disappeared
on intervening cached presentations.

The cache-hit path now jumps to the existing native overlay stage after the
world copy. Geometry merging, edge recovery, ray bins, texture caches,
paired-column drawing, worker boundaries, world caching, interpolation and
asynchronous worker recovery remain enabled. This is a composition fix;
it does not rebuild geometry on every presentation or disable the cache.

## Evidence

- The old build fails a regression that repeatedly presents a cached world
  containing a marked native sprite. The fixed build passes.
- Expanded checks change the sprite's pixel on each cached presentation,
  remove the sprite and check for ghost pixels, and verify that world-cache
  content remains unchanged and valid throughout.
- The renderer state suite and all six focused renderer CTest checks pass.
- Three identical 1,673-field widescreen routes produced 101 captures each.
  Every fixed cache-enabled capture is byte-identical to its cache-disabled
  reference. The old build differs from the fixed output in 47 captures.
  `before.png` / `fixed.png` retain one example, including a restored pickup.
- These capture runs overlapped background compilation. Their elapsed times
  are not used to claim a performance improvement or regression.

Normal build: `build/DoomSNESRecomp.exe`.
Previous normal/diagnostic executables and source/test copies:
`build-evidence/sprite-flicker-20261022/before/`.

Friend package:
`dist/DoomSNESRecomp-Sprite-Flicker-Fix-Report-20261022-Windows-x64.zip`.
It contains only the x64 game and its runtime assets/mods/licenses. The player
selects their own ROM; optional rotations require their own full Doom WAD.
Widescreen 16:9 / native resolution / 120 FPS interpolation are selected.
The previous geometry-fix diagnostic ZIP remains available separately.

Package smoke tests run the extracted archive with widescreen both enabled
and disabled, verify report identity/settings, validate all four report ZIP
entries against their source files and check native asynchronous worker health.
Detailed results, binary hashes and capture comparison CSV/JSON are retained
under `build-evidence/sprite-flicker-20261022/`.

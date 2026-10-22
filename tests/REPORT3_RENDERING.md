# Report 3: simulation recovered, rendering still expensive

The report executable hash matches the October 15 CPU-fix package. Across the
late window it maintained 60.0988 simulation fields/second, with one added audio
underflow and 414 missing audio frames. Earlier reports had hundreds of added
underflows and slower simulation. Routes differ, so these are observations of
each run rather than a controlled percentage improvement.

Report 3 still averaged 25.4103 presented pictures/second. Median picture spacing
was 37.54 ms, p95 82.41 ms and p99 99.95 ms. Normalized simulation work was
8.46 ms/field plus 1.68 ms capture; composition averaged 13.20 ms/picture.
Actual Direct3D 11 VSync was off despite the saved/requested VSync setting.
Presentation cost was about 0.57 ms/picture. CPU rendering remains the next
evidenced constraint. Neither these logs nor the earlier reports prove a
driver fault or a problem with the CPU's efficiency cores.

Two rendering changes retain the CPU scheduler and SuperFX fixes:

- Native resolution, level view: defer geometry/UV/mask capture until a host
  raster actually needs it. Vertical look stays enabled. Raster metadata is
  part of the camera cache identity, so changing pitch forces a suitable replay.
  Widescreen and increased resolution retain their metadata collection.
- Native resolution, tilted view: shade only newly exposed edge rows and rows
  containing holes from shifted messages. The native picture replaces the other
  rows afterward. Widescreen and increased-resolution raster paths are unchanged.

Two paired local runs with vertical look enabled showed composition falling
from 7.27-7.32 ms to 6.49-6.50 ms for a level view; total uncapped frame time fell
from 12.88-12.93 ms to 12.11-12.16 ms. These are local measurements, not a promise
of a particular FPS on the affected PC. An isolated simple tilted-world raster
fixture measured 189-196 ms for 1,000 old full pictures versus 144-148 ms for
exposed-row rendering. This is about 23-26% for that operation, not the game.

Validation:

- The final release executable matched all 278 captured image/state files and
  the full native guest trace against the preceding CPU-fix release.
- A frozen full-raster reference matched every output pixel in 56 combinations
  of fractional/integer pitch, messages, row visibility and empty geometry.
- Renderer lifecycle tests cover a level-to-tilted cache transition with no
  camera movement. The renderer state test and seven relevant CTests passed.
- Live trace-enabled runs passed both interpolation and original-cadence look:
  80 visible pitch changes versus 16 native capture changes across 80 fields.
  Those runs and the final release capture reported no presentation replay failures.
- The final packaged diagnostic executable completed a paced Direct3D 11/audio
  route, exited normally and generated all four report entries. ZIP extraction
  reproduced each file byte for byte; the report executable digest matched the
  packaged executable. No replay failures were logged. The local late window
  recorded 59.71 simulation fields/second, 79.28 pictures/second, two underflows
  and 224 missing audio frames. This smoke test does not establish an FPS gain
  on the affected PC or promise zero underflows.

The timing harness now has an explicit `-VerticalLook` switch and rejects replay
failures, which would otherwise time native fallback instead of the enhancement.
Earlier modern-control-only microbenchmarks left vertical look off by default;
use the explicitly named `report3-look-*` paired runs for the level-view claim.
A larger hook filter showed no clear benefit and was removed. Two intermediate
lazy-render runs used stale objects after that experiment and are invalid; they
are identified in `build-lag-evidence/report3-20261015/invalid-runs.txt`.

Raw reports, hashes, comparisons, benchmarks and original source copies are in
`build-lag-evidence/report3-20261015/`. Confirmation of the remaining visual lag
on the affected PC still requires a report from the new rendering build.

Package: `dist/DoomSNESRecomp-Render-Fix-20261015-Windows-x64.zip`. Extract it
separately, select a user-owned US ROM, play the same route including level and
tilted views, and close normally for the report ZIP. Keep the previous CPU-fix
package for binary rollback. A guarded source rollback preview is available via
`build-lag-evidence/report3-20261015/rollback.ps1`; `-Apply` restores the earlier
source while preserving the CPU/audio fixes. Restored source timestamps are
advanced so the next build cannot silently retain stale compiled objects.

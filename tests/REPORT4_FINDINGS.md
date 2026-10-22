# October 15: findings across four reports

Report 4's executable digest matches the delivered rendering-fix package
(`b589dedf6b2f1f1fdc5c7fc7828c2a7afb9c7f52fe676266e5b8b688deed8563`).
All four reports use the same i7-12700/RX 5700 XT PC, native resolution,
modern controls and vertical look. Report 2 requests 144 FPS; the others request
120. Actual Direct3D 11 VSync is off in every log. Saved/requested VSync is a
separate value, overridden by the high-frame-rate host pacing policy.

| Report/build | Simulation fields/s | Presented pictures/s | Added underflows | Missing audio frames |
| --- | ---: | ---: | ---: | ---: |
| 1 / original diagnostic | 50.250 | 21.210 | 765 | 146,929 |
| 2 / original diagnostic | 54.232 | 34.570 | 505 | 112,776 |
| 3 / CPU fix | 60.099 | 25.410 | 1 | 414 |
| 4 / rendering fix | 60.099 | 25.944 | 0 | 0 |

These are the recorded windows after console field 1400, not identical routes
or controlled before/after tests. Menus and pauses remain included. The player's
on-screen FPS observations can refer to individual gameplay sections rather
than these session averages. The late-window results prove recovery of the
simulation clock and audio in the observed fixed-build runs. They do not prove
that the rendering fix produced a general FPS increase: report 4 still has
essentially the same picture cadence as report 3. Report 4 logged no replay
failures. Its median picture interval is 36.39 ms, p95 80.48 ms, p99 98.86 ms.

## What has been fixed

- Scheduler: overdue native simulation/audio takes precedence over expensive
  presentation. This prevents the old self-sustaining backlog. Emergency video
  remains available during sustained CPU overload.
- SuperFX: packed 8-bit pixel-cache transposition preserves state/RAM and timing;
  release cross-file optimization reduces repeated helper-call overhead.
- Rendering: level views defer unused geometry/UV/mask capture; tilted views
  avoid shading rows subsequently covered by the native picture. Widescreen
  and higher-resolution behavior retain their existing paths.
- Verification: prior fixes passed 41 tests; rendering changes matched 278
  image/state files plus the guest trace, and 56 full-image raster cases. Look
  cadence was checked in the running trace-enabled game. Microbenchmark gains
  apply to their measured operations, not the entire affected-PC session.

## What remains

Report 4's normalized native work is 8.520 ms/field plus 1.683 ms capture.
Composition is 13.001 ms/picture; driver presentation is 0.585 ms/picture.
The serialized host-thread stages account for about 0.613 seconds of simulation
and capture and 0.352 seconds of composition/presentation per elapsed second.
These are wall-time phase measurements, not independent CPU utilization samples.
They imply a serial budget of roughly 28 pictures/s at the observed average
costs, before other overhead. The observed 26 pictures/s is consistent with that
constraint. Changing only the requested cap cannot remove this work.

The next substantial performance work is reducing private SuperFX replay cost
or separating isolated rendering work from simulation while preserving snapshot
ownership, presentation history and input responsiveness. Current reports do
not distinguish geometry replay, draw replay and host raster cost. They also do
not establish an efficiency-core scheduling issue or a GPU/driver fault.

## Reporting limit found and corrected

The old timing buffer stopped at 36,000 pictures. In report 4 the CSV ends at
field 80,096 although the host ran through field 94,589: roughly 241 seconds of
the final run are absent. Its zero added audio gaps are proven for the retained
22-minute window, not for those final four minutes. The full-session host log
still reports about 60.068 native fields/s over 1,574.7 seconds.

Capture now keeps the latest 36,000 pictures in a bounded ring and writes them
chronologically after gameplay. The report states the retained field/time range,
prints normalized simulation/capture cost and cumulative audio counters, and
labels the phase timing scope. No in-game file writes or growing allocations
were introduced. Developer-only `SNESRECOMP_FRAME_TIMING_LIMIT=64` exercises the
same wrap path in a short run; production defaults retain the original memory
bound. The host log records retained, total and overwritten counts.

Diagnostic builds now separately time camera geometry replay, draw replay,
geometry/UV/sprite capture, native image decode and host raster. Camera-worker
times may overlap in widescreen; they are cumulative wall times, not additive
whole-frame timings. Normal builds omit these measurements.

Two real-game collection tests retained 1,983 rows by default and exactly the
latest 64 rows with forced wrap. Both ended at field 2,102, preserved chronological
order and finite nonnegative stage deltas, and had identical guest traces. That
trace also matches the preceding rendering-fix release. Renderer state/raster
regressions passed. Raw evidence, input hashes and original source snapshots are
under `build-lag-evidence/report4-20261015/`.

The reporting/profile changes are diagnostic improvements, not an additional
claimed gameplay FPS fix.

The final Windows diagnostic package completed the paced Direct3D 11/audio
route, exited successfully, reported the new coverage and phase sections and
logged no replay failures. The four report ZIP entries were verified byte for
byte after extraction and the executable digest matched the packaged binary.
Its local late window had 59.70 simulation fields/s, 81.73 pictures/s and three
audio underflows (694 missing frames); this is a smoke test, not proof that
all PCs are free of audio jitter. Package audit found one executable and no ROM,
saves or runtime logs.

Package: `dist/DoomSNESRecomp-Detailed-Report-20261015-Windows-x64.zip`.
It retains the rendering-fix gameplay changes, accepts the player's own ROM in
the launcher and produces the report on normal exit without PowerShell. A new
affected-PC report, with movement and up/down look, will distinguish the costly
replay and raster phases. Guarded source rollback is available in
`build-lag-evidence/report4-20261015/rollback.ps1`; preview first, add `-Apply`
to restore this reporting/profile change while preserving earlier gameplay fixes.

# Slow movement / turning jitter, October 15

Inspected `jitter.mp4` (13.78 seconds, 1920x1080, 60 FPS). Overview and a
consecutive-frame strip of the slow turn are retained in
`build-evidence/jitter-20261015/`. The active development profile has native
resolution, widescreen off and interpolation on, so it uses the worker.

## Cause and correction

The synchronous renderer rebases each new native interval on the last displayed
camera and interpolated RAM. This helps with unequal native render intervals.
With a worker, an older job can still be in flight when the native capture
chooses that origin. If the older job is displayed afterward, it can move the
visible camera ahead of the newly rebased interpolation. The next worker target
then moves backward. This affects position, yaw and interpolated scene state;
generation ordering alone cannot prevent it.

Native-view asynchronous interpolation now uses consecutive native endpoints
for both camera and previous RAM (including moving sectors/objects). A delayed
publication cannot change the origin of the newer interval. Synchronous modes
retain their existing rebase policy. The setting is refreshed during normal
renderer configuration, including fallback. Worker ownership, bounded queue,
replay checks and the native simulation remain unchanged.

`DOOM_CAMERA_TRACE=1` optionally logs requested/published camera poses to stderr
for investigation. It is off during normal use.

## Verification

The paced canonical E1M1 route turns continuously in one direction between
fields 1540 and 1631:

| Run | Accepted turn pictures | Forward yaw changes | Backward yaw changes |
| --- | ---: | ---: | ---: |
| Original timeline, instrumented | 85 | 66 | 14 |
| Fixed timeline, normal worker | 86 | 81 | 0 |
| Fixed timeline, synchronous oracle enabled | 45 | 44 | 0 |

Identical native trace SHA-256 in all three runs:
`4CB1E6B5449706861D305098220163CB1B40AEA4BF9270F1AF8040C1EB77A71F`.
Oracle mode intentionally adds main-thread replay work; its picture count is
not a performance comparison. These runs establish removal of the reproduced
camera reversals, rather than smooth 120 unique world pictures per second.
Ordinary worker reuse, native coordinate quantization and uneven frame timing
can still cause visible stepping.

Standalone renderer regressions, including the frozen raster references, pass.
The new cases exercise slow/fast position and yaw, both directions, angle wrap,
varying native intervals, delayed displayed poses and moving-sector RAM. The
same test fails when restored to the old presented-pose policy. The final live
vertical-look test produced 80 visible changes over 80 fields, verified 129
worker jobs, and reported zero mismatches or replay failures.

## Builds and rollback

The normal development executable `build/DoomSNESRecomp.exe` and trace-enabled
`build-lag-debug/DoomSNESRecomp.exe` were rebuilt. Main executable SHA-256:
`5383AF8F186B80C276350806EC735336E397EC05180E3DFAADA57161E7D9E744`.
User settings and saves were preserved. The separate report-6 diagnostic ZIP
has not been replaced and still contains the pre-fix renderer.

A runnable pre-jitter development build, including its existing profile and
saves, is under `dist/rollbacks/Development-before-jitter-*` (exact path recorded
in `build-evidence/jitter-20261015/rollback-path.txt`). The older October 11
rollback also remains. Source backups and regression evidence are in the
same jitter evidence directory; no prior uncommitted changes were discarded.

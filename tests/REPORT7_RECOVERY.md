# Report 7: permanent fallback after temporary overload

## Evidence

Report 7 matches the October 19 report executable:
`7139AFE95142AFE9975FA4739D872E83136F4346920147F3D4D190B4277CB70D`.
It uses the same i7-12700/RX 5700 XT, actual D3D11 VSync off, native resolution,
widescreen off and 120 FPS interpolation. Rotations, infighting, crosshair,
modern controls and vertical look are all active. The supplied files are unchanged.

The retained ring covers fields 114636–161763: 787.041 seconds, 36,000 pictures.
The whole session exceeds 161,000 native fields. The window average is 59.879
native Hz and 45.740 presented pictures/s, with two added underflows / 595 missing
audio frames. Lifetime counters show 54,251 accepted worker jobs, seven stale
jobs, and one fallback. Camera replay averages 18.459 ms/job versus about
18.040 ms/job in report 6; that small job-cost change does not explain the large
late-session presentation regression by itself.

The decisive log event is between fields 122160 and 122280:

`[doom-async-fallback] repeated obsolete jobs; using synchronous renderer`

Accepted jobs and worker reuse counters then stop advancing for the rest of
the session. The old implementation sets `async_disabled=true` for this
successfully-completed-but-obsolete-result condition, with no retry.

| Retained phase | Fields | Native Hz | Presented pictures/s | Mean composition | Native work/field | Raster/capture/field |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Worker still active | 114636–122040 | 60.101 | 78.89 | 1.469 ms | 9.003 ms | 1.745 ms |
| After fallback | 122407–161763 | 59.876 | 39.86 | 5.973 ms | 8.917 ms | 1.779 ms |

The transition interval is excluded from this table. Different gameplay remains
included, so these are observations rather than a controlled benchmark.
Nevertheless the worker counters freeze at the logged fallback, native work
stays similar, and composition becomes much more expensive. The report
establishes permanent loss of the fast path, rather than merely a lower FPS cap.
The active phase adds no underflows; the post-fallback slice adds one; the full
retained window adds two, including the excluded transition interval.

## Correction and geometry constraints

Completed obsolete jobs still cannot be displayed. On sustained obsolete
results, the idle worker is joined/freed, synchronous rendering handles a
two-second cooldown, and a new worker starts from fresh inputs. The restart
clears interpolation history so synchronous displayed-pose rebasing cannot
leak into the asynchronous native-endpoint camera timeline. Repeated overload
may still cause temporary slow intervals, but cannot permanently discard the
fast path for the remainder of an otherwise recoverable session.

Replay failures, oracle mismatches and the one-second in-flight watchdog retain
their hard-disable behavior. A hung/incorrect worker is not treated as ordinary
staleness. New report counters distinguish temporary fallbacks and retries.

Inspected `problem1.mp4` (33.35 seconds, recorded October 16). It includes
widescreen outdoor and pillar/wall views; report 7 uses widescreen off. This
older clip is a geometry-regression reference, not a recording of report 7's
native-view fallback event. Its frames are retained for comparison.

Neither `src/doom_renderer.c` nor `src/doom_resolution.h` was changed in this
task; their hashes match the previous geometry-fix validation. The update does
not remove merged geometry, recovered edges, ray/plane preparation, world cache,
sprite identity handling or dither repair. It does not loosen the epoch,
generation-age, monotonic-publication or complete-job acceptance conditions.
No partial or arbitrarily old camera output is accepted to increase FPS.
These constraints and tests preserve the repairs; they cannot prove absence
of every possible visual artifact across every map/hardware configuration.

## Tests

- Six targeted renderer/rotation/infighting/crosshair tests pass.
- A one-time 200 ms diagnostic worker delay reproduces completed-obsolete jobs.
  The original executable submits six jobs, accepts zero, falls back once and
  never resumes. The fixed build temporarily falls back once, retries once,
  accepts 233 jobs and verifies 239 private jobs with zero mismatches.
- After that recovery, the canonical continuous-turn section publishes 46
  pictures with zero backward yaw steps. Slow-turn jitter protection remains.
- A separate 1100 ms delay exercises the hard watchdog: one hard fallback,
  zero temporary fallbacks and zero retries, followed by normal shutdown.
- Native trace SHA-256 is identical in the original fault run, recovered run,
  watchdog run and extracted report-package smoke test:
  `4CB1E6B5449706861D305098220163CB1B40AEA4BF9270F1AF8040C1EB77A71F`.
- Fault runs are unpaced correctness tests, not FPS benchmarks. Synthetic delay
  and one-time delay controls are diagnostic-only and absent by default.
- A normal paced D3D11/audio run with all three mods active held 60.065 native
  Hz, accepted 534 jobs, and logged no fallback/retry/replay failure. Its
  measured fields 1400–1900 still had one underflow / 119 missing audio frames.
  This fix addresses permanent rendering fallback, not all audio hiccups.
- The extracted report ZIP runs with the worker enabled, generates exactly
  four byte-identical report entries, and has no editor/converter names in its
  executable. The distribution contains one game executable and no ROM, WAD,
  imported artwork, editor, converter, saves or personal ROM path.

## Delivery and follow-up

`dist/DoomSNESRecomp-Recovery-Report-20261019-Windows-x64.zip`
is a new separate package; the original report-7 package remains available.
Select the user's own `Doom (USA).sfc`, select their own full `DOOM.WAD` in Mods
for rotations, use the same native-resolution settings and revisit the heavy
area. Close normally and send the report ZIP. New `Worker recovery` counters
show whether the fast path restarted after overload. No PowerShell or GitHub
is needed.

Report executable SHA-256:
`36E695F8979ACAFDF8D38AA5F7C9E574E017F208FF4405E9C1611B3D56C6F855`.
Distribution ZIP SHA-256:
`B251758EB0CA5ED3580989CF006EECB74A84962DC90EC88D27DD327761D768AF`.

The normal development build is rebuilt without automatic reports. Its prior
runtime, settings and saves are preserved under the exact path in
`build-evidence/report7-20261019/rollback-path.txt`. Source backups, input hashes,
phase analysis and fault-test evidence are in that same evidence directory.
The friend's next run is still needed to establish recovery and visual behavior
on the affected PC, including any remaining overload or geometry issue.

Normal development executable SHA-256:
`C18FB13BEE60062CBE1832161D8E249AB95CBED6EA54EE1629CA7C63FE101BAC`.

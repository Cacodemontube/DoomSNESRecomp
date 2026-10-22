# Report 5: camera replay bottleneck and experimental worker

Report 5 uses the detailed-report executable delivered after report 4
(SHA-256 `44E56A4E1D648541AB618165CF56FEEA5BB1353C2C00D1209EEBFA367EF5CC3F`).
The PC is still the i7-12700/RX 5700 XT, native resolution, widescreen off,
120 FPS interpolation, modern controls and vertical look. Actual D3D11 VSync
is off. The supplied reports have not been modified.

## What the new evidence establishes

The retained window covers console fields 39502 through 92792, about 887 seconds.
It contains 36,000 presentations; the ring overwrote 25,516 older records.
Simulation averaged 60.071 fields/s and presentation 40.580 pictures/s.
Two underflows added 290 missing audio frames during this window. The session
average includes menus, stationary scenes and repeated pictures. It is not a
controlled improvement over reports 3 or 4.

The final gameplay is much worse than that average: approximately ten-second
windows near fields 90824 through 92632 fall to 3.76–3.78 pictures/s while
simulation remains near 60 Hz. Native work costs about 13.4 ms/field, plus
1.8 ms for capture, leaving little of the 16.6 ms field budget. Camera
composition costs about 19–21 ms in these windows. The scheduler's emergency
presentation cadence is consistent with this serial budget.

Across 22,544 camera jobs, mean job time is 17.667 ms: geometry replay 8.251 ms,
draw replay 9.207 ms, metadata 0.080 ms and decode 0.094 ms. Geometry and draw
consume about 98.8% of the job. Host raster calls average 0.410 ms. This points
to private SuperFX camera replay, rather than host rasterization or VSync, as
the next substantial target. It does not establish a GPU fault or CPU affinity
problem.

## Changes made

The experimental native-view renderer uses one persistent worker for private
SuperFX replay. Each job owns its ROM, current/previous RAM, snapshot and output;
it cannot read or mutate live emulator state. The main thread keeps native
simulation/audio running and publishes completed pictures. Only one job can be
in flight; the next request uses the latest target instead of building a queue.
Results may trail the latest capture by one native snapshot, but cannot move
backwards or cross an invalidated scene. Older results are rejected. Exact
generation matching starved completion publication under the injected load;
this bounded allowance trades some camera latency for forward progress.

Higher resolution, widescreen, automap, native menus and interpolation-off views
retain the synchronous renderer. Resource/configuration changes and shutdown
join the worker before freeing its inputs. Allocation failure, replay failure,
verification mismatch or sustained lack of progress selects the original path.
Stress testing exposed an overly sensitive three-obsolete-result guard; it was
replaced with a one-second sustained-progress check. A separate one-second
in-flight watchdog also applies.

`DOOM_ASYNC_PRESENTATION` defaults OFF in ordinary CMake configurations and ON
in this separate experimental diagnostic package. Developer environment settings
allow A/B runs and a synchronous oracle. Oracle mode compares complete RAM,
pixels, message masks, floor/object metadata, instructions and semantic scene
data. Diagnostic-only injected CPU work and worker delay support stress/fallback
tests; both are zero by default and must not be confused with real gameplay.

Reports count submitted, accepted, stale, cancelled and reused pictures,
verification mismatches and fallbacks. Presented FPS explicitly includes reused
pictures. Fresh-picture throughput must be assessed separately.

## Verification and interpretation

Before the final fallback-guard adjustment, 344 worker jobs matched their
synchronous oracle with zero mismatches. The native trace SHA-256 was
`4CB1E6B5449706861D305098220163CB1B40AEA4BF9270F1AF8040C1EB77A71F`, identical
to the previous reference. With the worker disabled, 278 image/state files and
the native trace matched the prior renderer. The frozen 56-case raster reference
test passed. Live tests exercised interpolated vertical look with worker
verification, native look cadence, automap projectile arrows, and higher
resolution fallback. Final-source checks and performance results are recorded
below when completed.

Initial local paired runs presented about 66–70 versus 98–110 pictures/s, but
fresh camera output remained around 50–57 pictures/s. That is not evidence of
a comparable increase in unique pictures. A separate controlled test injects
main-thread work per native field. These synthetic results do not predict
affected-PC FPS or reproduce the friend's workload exactly.

### Final source results

The packaged executable SHA-256 is
`480A7E3B36419860D96AC059996EB24F8A5FE64D000435BD32BCC1FFF2916010`.
The frozen renderer state/reference test passed again. A final oracle run
verified 343 jobs with zero mismatches/fallbacks and the identical native trace
hash above. The final live vertical-look test produced 80 visible changes over
80 fields, verified 129 jobs, and reported zero mismatches. A diagnostic
1100 ms worker delay exercised the one-second watchdog: one safe fallback,
normal shutdown, and the same native trace hash.

One local matched pair with **7 ms injected work per console field** measured:

| Path | Native fields/s | Presented pictures/s | Fresh camera pictures/s | Added underflows / missing frames |
| --- | ---: | ---: | ---: | ---: |
| Original synchronous | 59.93 | 34.64 | 28.60 | 0 / 0 |
| Experimental worker | 59.82 | 67.83 | 51.27 | 1 / 16 |

Native/presented rates and audio use fields 1400–1900; fresh output uses the
accepted-job/pass counter difference between fields 1440 and 1800, aligned to
the nearest CSV presentation. In this moving route every completed accepted
job is a new camera target. These are one pair of local runs, not a prediction
or statistically established speedup. With **10 ms injected work**, heavy
sections left no dependable presentation budget and obsolete jobs triggered
safe fallback. The worker cannot fix native simulation saturation. Those
rejected benchmark runs are retained rather than counted as successful worker
results.

A separate packaged D3D11/audio smoke run held 60.06 native fields/s, accepted
556 jobs with no replay failures/fallbacks, and generated the four-file report
ZIP. Its measured interval still had one underflow / 298 missing audio frames.
This does not establish an audio improvement from the worker. Automatic
reporting and the earlier native/audio protections remain present.

The test package is
`dist/DoomSNESRecomp-Parallel-Render-Test-20261015-Windows-x64.zip`.
Extract into a separate folder, double-click, select the user's own
`Doom (USA).sfc`, play the same problem area and close normally for the report.
No ROM, PowerShell or separate toolkit is required in the package.
The packaged default was also tested without the enabling environment override:
343 accepted jobs, zero fallbacks/mismatches, and the unchanged native trace.
Both smoke-report ZIPs contained exactly the four expected files, byte-identical
to their on-disk reports. The distribution archive was checked for one executable
and no ROM, saves or run logs. ZIP SHA-256:
`DA01BA1C1870EFDD0E8F4E67206B094742DDC50DCE59D5AB2DB8C7D1DCAB9F78`.

The earlier native scheduler/audio and packed SuperFX pixel changes remain in
place, as do the metadata/raster reductions and rolling diagnostic capture.
Report 5 supports the native-clock recovery; its two small audio underruns mean
audio is not completely proven solved. The new rendering pipeline still needs
the friend's test in the same heavy gameplay area.

Evidence, input hashes, fixture reports and guarded rollback live under
`build-lag-evidence/report5-20261015/`. Older rollback manifests intentionally
remain unchanged. This task preserves earlier uncommitted changes.

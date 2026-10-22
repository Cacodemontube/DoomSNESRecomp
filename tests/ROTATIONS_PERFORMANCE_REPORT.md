# October 19: restoring performance with rotations and keeping geometry repair

## Findings

Two regressions were present in the current development configuration:

- `build/CMakeCache.txt` set `DOOM_ASYNC_PRESENTATION=OFF`.
- Both asynchronous eligibility checks explicitly excluded enabled PC rotations.

Consequently the renderer that improved report 6 could not run with rotations,
even when the worker build option was enabled. Rotations require metadata and a
full host world pass, so their synchronous camera replay competes directly with
native simulation/audio. There is no new affected-PC report yet; these are
verified configuration/code findings, not a complete diagnosis of its latest
gameplay or a measured speedup on that PC.

## Implementation

Private replay workers now capture native sprite metadata without consulting
the rotation asset map. Once a complete result is accepted, the main thread
selects PC rotations using that result's private RAM/ROM and camera, before
world composition. Workers do not retain or access imported patch pointers.
Asset import/clear and palette updates therefore cannot race replay. The same
selection helper serves synchronous capture and deferred publication.

Rotations no longer exclude the native-view worker or its native-endpoint
camera timeline. The slow-turn backward-jitter fix remains active. The bounded
queue, generation checks, oracle, watchdog and synchronous fallbacks remain.

The merged geometry, edge recovery, ray bins, plane preparation, world cache,
and corrected floor dither were retained. No geometry work was removed to gain
speed. Widescreen and higher resolutions still use the synchronous renderer.
Worker use requires interpolation, native resolution and widescreen off.

The CMake worker default is now ON for new build directories. Existing caches
can override it, so the development and report builds were explicitly configured
ON. The development build retains IPO, turns the editor test integration OFF,
and does not generate automatic diagnostic ZIPs. Existing settings and saves
were verified unchanged.

The launcher no longer contains editor/converter launch buttons or the named
editor status message. The report build compiles editor integration OFF and
contains neither tool name in its executable. Separate tools/source were not
deleted. Reports now sample active rotations and enabled infighting/crosshair
before shutdown, so cleanup cannot erase those settings from the report.

## Validation

Six targeted CTest tests pass: infighting, crosshair, imported rotations,
resolution, native sprite/rotation capture and renderer state. Deferred rotation
selection matches synchronous sprite metadata for all tested actors/projectiles
and all eight directions, using the local user-owned ROM/IWAD.

With rotations, infighting and crosshair enabled, a private-job oracle run
verified 331 jobs with zero mismatches or fallbacks. The running native E1M1 / 
outdoor E3M1 doorway-and-turn test verified another 967 jobs, with zero mismatches
or replay failures. Edge recovery was active (nine edges at the sampled final
pose). Eight outdoor views were captured and visually inspected. Widescreen
fallback was separately exercised with all three mods: zero worker jobs,
zero replay failures, four recovered edges at its sampled final pose.
Static captures and these tests do not prove absence of every possible pop-in
across every map or establish perceived latency on the friend's PC.

One local paired paced D3D11/audio comparison, with all three mods active:

| Renderer | Native Hz | Presented pictures/s | Fresh camera completions/s | Added underflows / missing frames |
| --- | ---: | ---: | ---: | ---: |
| Synchronous | 60.063 | 66.31 | 52.46 | 1 / 104 |
| Worker | 60.061 | 113.03 | 59.92 | 0 / 0 |

Native/presentation/audio measurements use console fields 1400–1900. Fresh
rates align periodic pass/accepted counters at fields 1440 and 1800 to the next
CSV presentation. Presented FPS includes reused camera pictures; it is not the
fresh output rate. These are local single-pair observations, not a prediction
for other hardware. No compilation ran during this pair.

Native trace SHA-256 matched across the paired runs, oracle run and packaged
default/development smoke runs:
`4CB1E6B5449706861D305098220163CB1B40AEA4BF9270F1AF8040C1EB77A71F`.
Infighting remains enabled; this route's trace match is not a substitute for
its dedicated gameplay tests, which also pass.

## Delivery

Friend package:
`dist/DoomSNESRecomp-Performance-Report-20261019-Windows-x64.zip`.
It contains one executable plus game runtime assets, six bundled mod packages,
generic settings, instructions and licenses. No ROM, WAD, imported sprite art,
editor, converter, commands or personal settings are bundled. Extracted-package
testing verified the default worker and four byte-identical report ZIP entries:
`report.txt`, `timing.csv`, `stdout.log`, `stderr.log`.

The first comparison defaults to native resolution, widescreen off, 120 FPS
interpolation, modern controls, vertical look, infighting and crosshair.
Rotations start disabled until the player selects their own full Doom/Ultimate
Doom `DOOM.WAD` in Mods and enables them. No WAD is needed for native sprites.
The player selects their own `Doom (USA).sfc`, plays the problem area, and closes
normally; the newest ZIP opens under the usual LocalAppData Diagnostics folder.

Report executable SHA-256:
`7139AFE95142AFE9975FA4739D872E83136F4346920147F3D4D190B4277CB70D`.
ZIP SHA-256:
`657F0EAC914395CE56A1749CB8F0B62C9CF5E9570F9A5B269FDC08078B2F72EE`.
Normal development executable SHA-256:
`02A78C48F48F6BC79E351E20BA2146D1C78FC41B14FE96155DC54F5B3CE67DF6`.

Evidence and source backups: `build-evidence/rotation-performance-20261019/`.
The exact runnable prior development build is recorded in `rollback-path.txt`
and kept separately under `dist/rollbacks/Development-before-rotation-performance-*`.
Earlier rollback directories and prior uncommitted feature work are preserved.

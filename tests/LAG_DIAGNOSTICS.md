# Investigating lag

Use an optimized Release build for timings. The TCP developer build enables
additional instrumentation and is useful for attribution, but is not a
production performance baseline. In the current framework, enabling trace
forces CPU/SPC diagnostics and FULL audio history even when their cache
options are OFF. Frame fingerprint, dispatch history, and PPU/DMA history
options can still be disabled independently.

## Repeatable measurements

From the game repository root in PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/measure_lag.ps1 -RunLabel stock -Paced -Desktop -Audio
powershell -NoProfile -ExecutionPolicy Bypass -File tests/measure_lag.ps1 -RunLabel enhanced -Enhancements -Paced -Desktop -Audio
powershell -NoProfile -ExecutionPolicy Bypass -File tests/measure_lag.ps1 -RunLabel enhanced60 -Enhancements -Fps 60 -Paced -Desktop -Audio
```

Supply `-Rom <path>` for another location of the expected US dump. Each label
must be new. The harness stages an executable and mod packages into an ignored
`build-lag-evidence/<label>` directory with fresh settings and no saves. It
disables physical input and drives the canonical gameplay route through the
host script interface. **The test game deliberately walks, turns, fires, and
pauses without keyboard input.** It exits at the route's end.

`-WidescreenOnly` and `-InterpolationOnly` isolate those features. Enhanced
runs use Fit aspect and Auto presentation unless `-Fps` overrides it. Without
`-Paced -Desktop -Audio`, the run is an uncapped, silent, dummy-video benchmark.
The harness disables VSync in its private config to isolate host scheduling;
it does not establish the user's actual VSync/VRR or monitor scanout behavior.

The CSV retains per-presentation host phase timings in memory until exit.
`summary.json` summarizes completed-present intervals for simulation fields
1400 through 1900, and preserves binary and ROM hashes. Phases are accumulated
since the previous presentation, so several simulations can contribute to one
row. Means of these rows are not exclusive emulator subsystem costs.
Comparisons should alternate run order, avoid concurrent builds, and retain
raw results. `-Capture` saves presentations for fields 1480 through 1600;
`-StateDumps` retains the canonical guest dumps and per-field WRAM/CPU trace.
Neither capture option should be used for precision performance runs.

## TCP renderer and input telemetry

Configure with `-DSNESRECOMP_ENABLE_TRACE=ON`, build, and set
`SNESRECOMP_DEBUG_PORT=4389` before launching the developer binary. The shared
server uses line-based text requests and JSON responses. Send:

```text
game presentation_stats
interp_stats
get_controller
audio_stats
screenshot C:/path/to/gameplay.bmp
```

`game presentation_stats` is a read-only Doom command. It returns the latest
published host-frame snapshot, actual P1/P2 masks submitted to the guest,
renderer support/snapshot state, captures, camera passes, cache hits, replay
failures, replay instruction count, and snapshot interval in console fields.
The draw-call count and total/maximum milliseconds measure Doom's custom
presentation callback, including its stock fallback, for the process lifetime.
They exclude upload/present and are not monitor scanout or input latency.
Renderer counters follow the renderer's reset lifetime. A reset may therefore
reduce renderer counters while draw totals continue increasing.

Doom's private render passes omit their unused instruction trace-ring writes.
They retain exact instruction counts, PC hooks, hardware execution and guards.
The authoritative native Super FX core and the existing shared replay APIs
continue to record instruction history. `superfx_replay_snapshot_with_history`
provides the explicit private-pass choice for equivalence tests and diagnostics.

Telemetry is published by the host thread after simulation capture and each
draw. A short spinlock protects the copied snapshot; the TCP thread does not
read live renderer workers. Performance-counter sampling and publication are
compiled out in production. No per-frame telemetry file IO is added. The TCP
handler rejects arguments and leaves unknown game commands to the framework.

`get_controller` reports TCP overrides. `p1_input`/`p2_input` in Doom's query
report the actual last submitted guest masks, including script or physical
input. A nonzero movement mask while idle helps distinguish held input from
movement produced by game logic. Pause/freezing may leave a last-frame value;
these masks are snapshots, not an input-event history.

## Reference emulator

The shared `snesrecomp/tools/snesref` frontend loads a separately supplied
Snes9x Libretro core. See its README for SDL2 setup and deterministic script
variables. Use the same ROM and `tests/presentation_route.txt`, set
`SNESREF_HEADLESS=1`, `SNESREF_WRAM_FILL=0`, and put dumps/traces/audio in an
ignored build directory. A stock core supports framebuffer, WRAM, VRAM and
SRAM capture; extended PPU-register exports require a patched core.

Matching script field numbers are useful workload windows, not proof that
both implementations have identical hardware boundaries or state. Align
hardware events before making correctness or timing-equivalence claims.

## Weapon diagnostics

The renderer snapshot also reports weapon update/interpolation counters, offsets,
tile count, layout fallback count, translucency, and native invisibility ticks.
The developer-only command `game weapon_invisibility_test <ticks>` accepts
0 through 1800 ticks and queues a native timer update on the emulation thread.
Use it only in isolated test sessions: unlike presentation_stats, it changes
active gameplay state. It is absent from production builds. The fixture verifies
translucency, native expiry blinking, and restoration of the opaque sprite.

Timing CSVs include cumulative audio_underflows, audio_missing_frames,
audio_dropped_audible and audio_occupancy. Compare gameplay deltas; initial
startup counts are not new gameplay starvation. CaptureFrom and CaptureTo allow
selecting a firing window separately from the default screenshot interval.

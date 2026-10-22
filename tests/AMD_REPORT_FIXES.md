# October 15 CPU and audio fixes

The supplied RX 5700 XT report used Direct3D 11, VSync off, native resolution,
120 FPS interpolation and modern controls. Its late window includes menu pauses.
Per-presentation guest timings include multiple simulation fields: normalized
guest work was about 9.79 ms per field, capture 1.71 ms per field, composition
16.75 ms per picture and GPU presentation 0.61 ms per picture. These measurements
point to CPU pressure; they do not establish a GPU-driver or hybrid-core fault.

The host scheduler previously forced expensive pictures while simulation was
already overdue. The new rule catches simulation up first, with an emergency
picture after a quarter second to keep sustained overload visible. A modeled
11.5 ms simulation / 16 ms picture workload now maintains approximately 60
simulation fields per second at presentation caps 60, 120 and 360. This regression
fails with the previous scheduler. Lower picture rates remain possible when
the selected presentation work exceeds the machine's CPU budget.

The SuperFX 8-bit pixel-cache flush now transposes eight pixels into bit planes
using packed operations. Other depths keep the original loop. All 65,536 tested
combinations match original core state and RAM, including pending memory buffers.
The isolated operation measured roughly 2.3 times faster locally; that is not a
whole-game speedup. Release interprocedural optimization is optional and enabled
for the private build; paired local runs reduced guest time by roughly 3–6%,
with a smaller and noisier overall throughput improvement.

All 41 tests passed. An existing radius-filter test was corrected to observe
the hook's prefix before STOP clears it; production behavior was unchanged.
Both the initial and final IPO gameplay comparisons matched 278 image/state
files and the full guest trace byte for byte. Evidence and original source snapshots are retained in
`build-lag-evidence/amd-report-20261015/`.

The Windows diagnostic package accepts the player's own US ROM through the
normal launcher and creates a report ZIP on normal exit. No PowerShell is needed.
The final package completed a paced Direct3D 11/audio route on the local NVIDIA
PC, exited successfully and created all four report entries, each verified byte
for byte after ZIP extraction. Its late window recorded 59.75 simulation fields
and 79.07 pictures per second, two added audio underflows and 680 missing audio
frames. This is a local smoke test, not a claim that audio starvation is eliminated.
The report executable hash matched the packaged executable. The package audit
found one executable and no ROM, saves or runtime logs.

An additional deliberately extreme CPU-contention check pinned each game and
two busy processes to the same logical processor, with matching configuration,
route, Direct3D 11, audio and interpolation settings. One baseline/fixed pair
recorded approximately 36.14/44.59 simulation fields per second and 11.22/5.39
pictures per second in the gameplay window. Missing audio frames fell from
13,394 to 7,262 over that route window. This verifies the real runtime's priority
tradeoff under contention, not a general speedup or reproduction of the friend's
machine. Long OS scheduling quanta in this setup also delayed the audio thread;
both runs remained overloaded. All helper processes were stopped afterward.
The reproducible harness and raw runs are retained alongside the other evidence.

Package: `dist/DoomSNESRecomp-CPU-Fix-20261015-Windows-x64.zip`.
Testing on the affected PC is still required to establish the actual improvement.
Keep the previous October 14 package for binary rollback.

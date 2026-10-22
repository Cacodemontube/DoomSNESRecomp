# Automatic Windows diagnostic build

`DOOM_DIAGNOSTIC_BUILD=ON` adds a Windows-only report wrapper to the game's
normal main entry point. It is OFF by default; the normal build was verified
with the option OFF and its presentation test passed.

The private test package is
`dist/DoomSNESRecomp-Diagnostic-20261014-Windows-x64.zip`. Extract it into its
own folder, double-click `DoomSNESRecomp.exe`, select a user-owned US ROM, play
with the settings that cause lag for at least one minute, then close normally.
The report folder opens automatically. Each session has its own report ZIP.
Interpolation at 120 FPS and modern controls are enabled initially; widescreen
and increased resolution can be selected in the launcher's Mods page.

Reports are saved under `%LOCALAPPDATA%/DoomSNESRecomp/Diagnostics`. The native
ZIP writer packages only `report.txt`, `timing.csv`, `stdout.log` and
`stderr.log`; no external compressor, PowerShell, ROM, save or configuration
file is included in a report. Timing data is retained by the existing host
profiler and written at exit; compression/archive IO occurs after gameplay.
The report records CPU/GPU/driver, executable SHA256, relevant settings,
late-window timing averages and audio-counter deltas. Late-window summaries
can still include menus or pauses and are not a measure of input latency.
Short sessions are explicitly labelled insufficient for a late-window summary.
Closing with End Task or a crash may leave only partial logs without a ZIP.

Validation: a fresh packaged runtime completed the canonical gameplay route,
reported the expected 120 FPS/modern-control selections, and generated all four
report entries. Native ZIP extraction reproduced each original file byte for
byte. The report's executable digest matched the binary that ran. The latest
binary also passed a two-field session that generated a report and correctly
marked insufficient timing data. This proves collection on the local NVIDIA
PC; the affected RX 5700 XT still needs to run it.

For unattended developer checks only, `DOOM_DIAGNOSTIC_NO_UI=1` suppresses the
instruction/result dialogs and Explorer, and `DOOM_DIAGNOSTIC_REPORT_ROOT`
selects an existing/writable report root. Users need neither variable.

The special package uses a separate configuration/save directory. Binary
rollback simply means closing it and returning to the normal installation.
For source rollback, original copies and a guarded rollback script are retained
in `build-lag-evidence/diagnostic-special-20261014/`. Preview with
`powershell -NoProfile -ExecutionPolicy Bypass -File build-lag-evidence/diagnostic-special-20261014/rollback.ps1`,
then add `-Apply` to restore only this diagnostic-build change. Subsequent edits
cause the script to refuse restoration. This source rollback returns the
earlier optimization rollback script to its original expected state.

# Report 6: parallel renderer behaving well

The executable digest matches the delivered parallel-render test:
`480A7E3B36419860D96AC059996EB24F8A5FE64D000435BD32BCC1FFF2916010`.
Same i7-12700/RX 5700 XT, native resolution, widescreen off, 120 FPS
interpolation, modern controls and vertical look. Actual D3D11 VSync is off.
No supplied report files were modified.

## Recorded results

The process exited normally after 37,660 console fields, about 627 seconds of
simulation service. The timing ring retained 36,000 pictures across fields
13269–37660, 405.847 seconds; it omitted 19,266 earlier timing rows.

| Metric | Report 6 |
| --- | ---: |
| Native simulation | 60.099 fields/s |
| Presented pictures | 88.701/s |
| Added underflows / missing audio frames in retained window | 0 / 0 |
| Cumulative audio at last sample | 1 underflow / 148 missing frames |
| Main-thread composition | 0.995 ms/picture |
| Driver presentation | 0.455 ms/picture |
| Native simulation work | 8.438 ms/field |
| Raster/capture | 1.687 ms/field |
| Worker submitted / accepted / stale | 20380 / 20375 / 5 |
| Worker cancelled / fallbacks / logged replay failures | 0 / 0 / 0 |

The one cumulative audio underflow predates the retained timing window; these
files cannot assign it to a specific early event. The worker's oracle was off
(`verified=0`), as intended for performance testing. Thus zero reported
mismatches is not new equivalence verification; those checks were performed
locally before packaging.

## Cadence beyond the average

Complete approximately ten-second presentation windows range from 76.43 to
118.17 pictures/s. No report-5-style sustained 3.8 FPS section appears in this
retained run. Individual intervals still vary: median 12.88 ms, p95 20.05 ms,
p99 22.22 ms, maximum 120.32 ms. Eighteen of 35,999 intervals exceed 50 ms.
This is healthy overall throughput, not perfectly even 120 Hz presentation.

Aligning periodic accepted-job counters to the first retained CSV row at or
after each log field gives approximately 33.09 accepted camera completions/s
over fields 13320–37560. Roughly ten-second windows with more than one accepted
job/s range from 26.92 to 41.16, median 34.37. Most ordinary active windows are
around 30–41. This measures completed camera targets, not pixel-by-pixel unique
images, exact visual latency or display scanout. The 88.7 presented figure
includes reuse of previous camera pictures and other native overlays.

Near fields 19320–20520 the accepted count almost stops, but native capture
and weapon counters stop as well. Captures remain at 2863 and accepted results
at 10469 through the quiet middle. Both resume by field 21120 without a worker
fallback. This is consistent with leaving active world rendering (for example
a menu, map or pause), rather than evidence of a stuck worker. The logs do not
identify the exact UI state.

## Assessment and remaining work

Keep this build for continued testing. This report shows sustained worker
operation, normal native speed and clean audio throughout the retained window.
It supports the intended architectural improvement. It is not a controlled
before/after FPS comparison: report 5 has a different route/session and much
heavier final native work. Absence of its severe drop here does not prove that
the exact old problem area is solved.

Worker camera jobs still average 18.040 ms: geometry 8.430 ms and draw 9.404 ms,
with metadata 0.082 ms and decode 0.094 ms. Host raster calls average 0.368 ms.
Moving replay off the main thread reduced composition pressure; it did not
make SuperFX replay itself faster. Raising fresh-camera cadence further calls
for replay optimization and measurement of camera latency/frame pacing.
Saturation of the native simulation thread remains a separate limitation.

No new gameplay code change or replacement package is justified by this report
alone. The useful remaining confirmation is how the same previously heavy area
and looking/turning feel to the player. Report logs cannot establish visual
correctness or perceived input latency.

Analysis scripts, input SHA-256 hashes, ten-second windows and accepted-job
rate estimates are in `build-lag-evidence/report6-20261015/`.

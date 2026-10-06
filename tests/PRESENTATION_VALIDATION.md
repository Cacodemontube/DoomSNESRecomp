The PowerShell harness runs a supplied Doom build against the player's US ROM
using SDL's dummy video/audio drivers. It copies the executable, SDL3 DLL,
staged mod packages and a deterministic input route into a fresh directory
under ignored `build-validation/`. It creates fresh host settings and mod
selections, and never copies saves or changes the source build's settings.

From the repository root:

```powershell
$rom = 'F:/path/to/Doom (USA).sfc'
./tests/validate_presentation.ps1 -BinaryDirectory ./build-clean -Rom $rom -RunLabel baseline
./tests/validate_presentation.ps1 -BinaryDirectory ./build-enhanced -Rom $rom -RunLabel enhanced-stock -CompareRun ./build-validation/baseline
./tests/validate_presentation.ps1 -BinaryDirectory ./build-enhanced -Rom $rom -RunLabel wide-169 -Aspect 16:9 -CompareRun ./build-validation/baseline
./tests/validate_presentation.ps1 -BinaryDirectory ./build-enhanced -Rom $rom -RunLabel wide-120 -Aspect 16:9 -Fps 120 -Paced -CompareRun ./build-validation/baseline -Screenshots -ScreenshotFrom 1620 -ScreenshotTo 1760
./tests/validate_presentation.ps1 -BinaryDirectory ./build-enhanced -Rom $rom -RunLabel fit-144 -Aspect Fit -Fps 144 -WindowSize 1920x1080 -Paced -CompareRun ./build-validation/baseline
./tests/validate_presentation.ps1 -BinaryDirectory ./build-clean -Rom $rom -RunLabel baseline-death -RouteFile ./tests/presentation_death_route.txt -MaximumFrames 2800
./tests/validate_presentation.ps1 -BinaryDirectory ./build-enhanced -Rom $rom -RunLabel enhanced-death -RouteFile ./tests/presentation_death_route.txt -MaximumFrames 2800 -Aspect 21:9 -Fps 120 -Paced -CompareRun ./build-validation/baseline-death
```

`-Aspect` enables the widescreen feature; accepted values are `4:3`, `16:9`,
`21:9`, `32:9` and `Fit`. `-Fps` independently enables interpolated
presentation at `Auto`, `60`, `90`, `120`, `144`, `165`, `240` or `360`.
Omitting either switch keeps that feature disabled. `-ModState <path>` instead
copies a prepared standard Mods `state.toml` into the isolated catalog.
`-RouteFile` accepts another input route; `-MaximumFrames` provides a safety
limit. Each run label must be new so earlier evidence remains intact.

The harness enables renderer diagnostics and records support, snapshots,
camera passes, cache hits and replay failures in the summary. When `-Aspect`
or `-Fps` requests an enhancement, it requires a supported renderer, at least
one completed camera pass, and zero replay failures. `-AllowRendererFallback`
opts out for an intentional unsupported/non-gameplay route. Prepared
`-ModState` files still record these counters; inspect them according to the
features selected in that state.

Use `-Paced` when checking interpolated presents and observed frame rates.
Without it, frame delay is disabled to finish guest-state checks quickly.
Screenshots add file IO overhead and should be omitted from performance runs.

The route cold-boots through logos/title/menu transitions into E1M1, fires
with Y, walks forward, turns, walks while turning, and pauses/resumes. Each
pause/resume Start press lasts 24 fields so the slower native game loop reads
it. The harness verifies that retail WRAM `RLFlags` at `$2B` clears the
game-not-halted bit `$4000` during pause and sets it again after resume. Each
`dump` records its exact completed field in `dump-frames.csv`; compare captures
by that field rather than by the wall time or present number.

The optional `presentation_death_route.txt` approaches a barrel and shoots it
to trigger death, then invokes the host's normal reset path. The stock visual
fix build currently logs architectural BRKs and produces a black picture
after reset; this is inherited regression evidence tracked as `beads-9reo`.
The harness records the
BRK count in its summary. A successful script exit or a matching guest digest
does not establish that reset booted correctly. Inspect the reset capture
separately, and use death to check that interpolation history is invalidated.
Reset also restarts the console's field counter: the host trace continues
counting run fields, while dump metadata reports the reset console counter.

Each run preserves `config.ini`, `route.txt`, `requested-mod-state.toml`,
binary/ROM/route hashes, logs, per-field WRAM/CPU fingerprints, full guest
memory snapshots (including Super FX cartridge SRAM), PPU registers and
256-column framebuffer BMPs. `-Screenshots` also writes complete presented
PPMs, and `presents.csv` records the guest field, interpolation alpha, pixel
CRC and mean luma for every captured present. Several presents may share one
guest field when interpolation is enabled.

`-CompareRun` requires identical ROM and route digests, compares every-field
WRAM/CPU fingerprints and all guest snapshot/register hashes, and exits with
an error if they differ. Framebuffer hashes are recorded separately because
presentation is expected to change. Inspect full PPMs to verify widened
geometry, HUD/menu placement, and interpolation; the stock state-dump BMP
contains only the authentic center 256 columns.

The geometry, projection and host-adapter tests run through CTest. Set
`-DDOOM_TEST_ROM=<path to your US ROM>` when configuring CMake to include the
renderer lifecycle/raster test as well. All checks remain active in Release
builds.

To investigate a private replay failure, set `DOOM_RENDER_FAILURE_DUMP` to a
file inside an ignored build directory. The first failing scene is captured
there. Build the optional `doom_renderer_replay_probe` target and run it with
the ROM path and fixture path as its two arguments. Fixtures are local,
same-build diagnostics containing game state; do not commit or distribute
them. The probe uses the real Super FX core to reproduce a scene without
booting the game again. It verifies expected task completions, unchanged
captured state, identical serial/threaded pictures, and worker stop/restart.

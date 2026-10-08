Doom SNES - PC Port (By Cacodemontube)
[Created with SNESRecomp]

This project is still not fully functional but you can check it here for research...

## Display enhancements

Open **Mods → Doom Display Enhancements** in the launcher. Both features
are disabled by default and can be enabled independently:

- **Adaptive widescreen** renders additional world geometry at 4:3, 16:9,
  21:9, 32:9, or **Fit to window**. Fit follows the window from 4:3 through
  32:9. The original status bar and weapon stay centered.
- **Interpolated frame rate** interpolates the camera and weapon sway between
  captured game states. Choose **Auto** to follow display refresh, or 60, 90, 120, 144,
  165, 240, or 360 presentations per second. The simulation and game speed
  remain unchanged. This works with widescreen disabled, too.

Use **Ctrl+L** to reopen the launcher during play. The framework saves the
selected options in its normal mod state beside the executable. Presentation
rates are targets; actual throughput depends on the cost of rendering the
scene and the host machine.

The renderer captures the original Super FX camera and scene, then executes
the game's render jobs against private copies of cartridge RAM. Additional
camera views are projected onto one flat wider view. Menus and unavailable
scenes use the centered original picture. No ROM, generated game code, or
game assets are included in this repository.

See [presentation validation](tests/PRESENTATION_VALIDATION.md) for the
repeatable gameplay route, guest-state comparisons, and frame captures.
Set `DOOM_RENDER_STATS=1` to log renderer capture/replay counters while
investigating a rendering problem.

See [lag diagnostics](tests/LAG_DIAGNOSTICS.md) for isolated timing runs,
reference-emulator comparison, and the developer TCP
`game presentation_stats` renderer/input query.

The local lag fixes correct beam/APU overshoot accounting and omit unused
instruction-history writes during private rendering replays. Both preserve
the player's input configuration; see [fix validation](tests/LAG_FIX_VALIDATION.md).

## Windows frame composition

The pinned shared framework uses cached HLE frame composition by default on
Windows x64. This optimizes host presentation while preserving the existing
guest CPU/Super FX, audio and status interfaces. Build the maintained
correctness-reference compositor separately with
`cmake -S . -B build-frame-lle -DCMAKE_BUILD_TYPE=Release -DSNESRECOMP_FRAME_IMPL=LLE`,
then `cmake --build build-frame-lle`. Selection is fixed at build time.
LLE can reduce performance; it remains available for correctness checks. Other
platforms keep LLE defaults, and existing CMake cache selections are preserved.
See [HLE defaults and opt-out](snesrecomp/docs/HLE_DEFAULTS.md).

The reviewed native-view, uncapped Windows route measured 250.122 to 443.316 FPS
(+77.24%, process CPU -43.12%); it includes boot/menu work and active gameplay.
The owner accepted the normal-paced adaptive HLE build. These are measured
build/route results, not universal gains or a quiet-host precision claim;
foreign compiler activity was observed. Normal play retains normal pacing/audio.
See the shared framework's [frame model](snesrecomp/docs/FRAME_MODEL_HOSTS.md).

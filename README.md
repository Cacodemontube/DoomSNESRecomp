## Doom SNES - PC Recompilation Project (By Cacodemontube)
[Created with SNESRecomp]

A still in development, native PC port of SNES Doom! 
With a bunch of quality of life improvements and much more to come!


## Display enhancements

Open **Mods → Doom Display Enhancements** in the launcher. All three features
are disabled by default and can be enabled independently:

- **Adaptive widescreen** renders additional world geometry at 4:3, 16:9,
  21:9, 32:9, or **Fit to window**. Fit follows the window from 4:3 through
  32:9. The original status bar and weapon stay centered.
- **Interpolated frame rate** interpolates the camera and weapon sway between
  captured game states. Choose **Auto** to follow display refresh, or 60, 90, 120, 144,
  165, 240, or 360 presentations per second. The simulation and game speed
  remain unchanged. This works with widescreen disabled, too.
- **Higher 3D resolution** offers 2x, 3x and 4x world rasterization. At 4:3,
  the 3D viewport is 432x288, 648x432 or 864x576 respectively. Widescreen
  increases its width further. Wall geometry and original wall textures are
  sampled at the new resolution. Objects are sampled directly from their
  original artwork at the selected
  resolution, with filtering for distant objects. HUD and weapon artwork retain
  their original pixel art. This works independently of the other display toggles.
  Start with 2x; higher levels increase CPU cost. Disable the toggle to return
  to native world rendering. Save data and gameplay speed are unaffected.

See [resolution implementation and validation](tests/HIGHER_RESOLUTION.md).

## PC keyboard cheats

Enable **Mods → Doom PC Keyboard Cheats → PC keyboard cheats**. It is off
by default and works with either the original controls or modern controls.
Type the codes directly during gameplay; no console is needed.

| Code | Effect |
| --- | --- |
| `IDDQD` | Toggle god mode with the glowing-eye face. Enabling sets health to 100%; disabling keeps your current health. |
| `IDKFA` | All weapons, full ammo, 200% armor, and all keys. |
| `IDFA` | All weapons, full ammo, and 200% armor; existing keys are retained. |
| `IDCLIP` | Toggle wall noclip. |
| `IDDT` | Cycle full map, full map with living monsters as green arrows, and normal map. |
| `IDCLEVxy` | Warp to episode `x`, mission `y`, with a black-screen transition and pistol start at the current difficulty. |

Available warp destinations are E1M1–5, E1M7–9; E2M1, E2M3–4, E2M6,
E2M8–9; and E3M1–4, E3M6–9. Missing SNES levels are rejected. Exiting a
warped level follows the game's normal progression and secret-exit rules.
Disabling the mod also disables god mode and noclip. Granted inventory stays.
Typing is ignored while a host panel is open or the game is unfocused.
See [cheat validation](tests/CHEATS.md).

## Modern keyboard and mouse

Enable **Mods → Doom Display Enhancements → Modern keyboard + mouse**.
The option is off by default. Set player 1's input source to **Keyboard**.

| Input | Action |
| --- | --- |
| W / S | Walk forward / backward |
| A / D | Strafe left / right |
| Shift | Run |
| E | Open doors and use switches |
| Enter | Pause / resume the game |
| Space | Jump |
| Mouse left / right | Turn, including while strafing |
| Mouse up / down | Classic vertical look |
| Left click | Fire |
| Right click | Use |
| 1 | Fist, or chainsaw if picked up |
| 2 | Pistol |
| 3 | Shotgun if picked up |
| 4 | Chaingun if picked up |
| 5 | Rocket launcher if picked up |
| 6 | Plasma rifle if picked up |
| 7 | BFG 9000 if picked up |
| Tab | Automap |
| Home | Center the view |
| Escape | Release the mouse; click to capture again |

Move the mouse up/down to navigate the original menus; left-click confirms.
Arrow keys and E also work. Ctrl+L opens the launcher. Legacy controls retain
the original SNES weapon cycling; modern controls use the number keys.
Sensitivity, invert-Y and the vertical-look toggle are in the same feature.
Mouse capture releases when focus is lost or a host panel opens.

Horizontal turning uses the original SNES mouse conversion as its base.
Vertical look shifts the rendered horizon in the style of classic FPS games.
The HUD and weapon stay fixed, and shooting retains SNES Doom's auto-aim.
It works at native resolution, higher resolutions and widescreen. Vertical
look uses the host world rasterizer and adds some rendering cost.
See [implementation and validation](tests/MODERN_CONTROLS.md).

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

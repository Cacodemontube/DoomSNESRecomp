# Video freezes and mod overhead

## 2026-10-11: reproducible menu failure after keyboard cheats

The `R` in IDCHOPPERS reached the desktop `ToggleRenderer = r` shortcut.
Doom blocked the host's P pause shortcut, but had no equivalent R guard.
The reference PPU does not extract the isolated BG2/OBJ surfaces that the
enhanced compositor draws over its replacement world. The native menu ran
and dimmed the scene, but its text and skull were overwritten. This also
reproduces on a cold boot with `NewRenderer = 0`.

v0.01 already had the unguarded R diagnostic, but did not implement
IDCHOPPERS. Adding that code exposed the shortcut collision. Reverting the
cheat's inventory writes would not fix the renderer selection at startup.

Doom now consumes unmodified R key presses/releases (including Shift+R)
before host shortcuts; Ctrl+R reset remains available. Its PPU preparation
also reapplies the span renderer required by its overlays each field.

`tests/validate_menu.ps1` checks screenshot pixels at startup, twelve
chainsaw/pause/resume cycles with different timings, held menus, cursor
movement, skill and episode pages. It grants the cheat at the native tick
and separately sends its spelling through SDL events, since headless SDL
has no keyboard focus. The pre-fix executable fails both the keyboard and
legacy-startup cases (77 red pixels in the menu area); the fixed executable
retains approximately 2,900 menu-text pixels and passes the submenu checks.
The renderer state test also checks that preparing a legacy-configured
PPU restores overlay support without dropping the sprite-limit setting.

One sequential, unpaced 16:9/4x movement comparison over fields 1400-1900
measured raster capture at 3.069 ms before and 0.852 ms after; total mean
interval changed from 24.832 to 22.533 ms. This is one local pair, not a
general FPS guarantee. Private rendering cost remains approximately
14.8 ms in that route. Artifacts are under `build-lag-evidence/` in
`menu-keyboard-before-20261011`, `menu-legacy-before-20261011`,
`menu-regression-final-20261011` and `ppu-legacy-*-20261011*`.

The reported laptop was not available for testing. This audit identifies
code paths consistent with the report, rather than proving its exact cause.

## Drawing could be postponed indefinitely

Presentation enhancements preserve simulation timing debt. The host clock
previously rejected every presentation while simulation was overdue. If a
machine needed more than one field period to simulate each field, it could
remain overdue forever: gameplay and audio advanced while the displayed
picture stopped updating. Changing the SDL renderer or VSync would not
remove that scheduling condition.

The clock now allows an overdue presentation after two frame periods (using
the slower of simulation and presentation). This bounds scheduling starvation;
an individual slow draw or guest field can still take additional time.
The overload regression simulates 25 ms guest fields and 10 ms draws at
60 Hz. Video continues updating for two seconds instead of receiving zero
presentations. Existing audio-priority and pacing tests still pass.

## Every GSU instruction searched mod observers

`run_pc_hook` previously searched every installed observer on every Super FX
instruction. Cheats, modern controls and private presentation replays could
increase the search cost even when no callback applied to that instruction.

A 256-bit filter now rejects unrelated address low bytes before searching.
The full 24-bit address comparison still handles collisions. Registration,
removal, resets and private replay setup rebuild the filter; it is host policy
and is excluded from saved guest state. Tests cover collisions across banks,
redirection, rearming, reset, save/load and replay history.

A local synthetic run of two million identical GSU instructions with 24
unused observers took 40 ms before and 29 ms after. Both reached the same
instruction count and PC. This is a hook-cost measurement, not a whole-game
frame-rate claim or a measurement of the user's laptop.

## Presentation mods still perform additional CPU work

Widescreen renders private side-camera passes. Interpolation can request
additional camera passes, and higher resolution samples geometry and textures
on the CPU. Those features cannot be assumed to have zero performance cost.
They can be limited by CPU work even when GPU utilization is low. The fixes
remove unnecessary observer searches and prevent video starvation; they do
not guarantee full speed with every enhancement on every computer.

## Menu boundary

The native IRQ phases can reach the menu call before the final GSU task
releases ROM. The verified USA retail menu boundary now waits for that task
before entering cartridge code. Menu OBJ capture also takes precedence over
gameplay weapon capture during a transition, retaining the skull cursor.
Runtime captures cover the main menu, movement of the skull, and skill and
episode pages with audio, modern controls, widescreen and 4x rendering.

The menu's native entry and return are tracked explicitly. Stationary menu
frames discard previous gameplay debt so text uploads can be presented
promptly. TCP screenshots must allow the main thread to finish presenting
after a step reply; otherwise they can copy the preceding composed frame.

## Follow-up rendering work

Raster threads retain immutable ROM texture-column caches between draws.
Vertex transforms reuse the camera's sine/cosine. Edge recovery indexes
directed vertex pairs and sectors instead of repeatedly searching the growing
segment list. The index retains the original directed-edge duplicate rule.

Native fades keep a fresh matching level snapshot and use brightness observed
during scanout, including after the journal restores post-field registers.
Music uploads retain a composed picture, suppress menu overlay classification,
and avoid repeating world rasterization while the loader runs. The native APU
upload itself still pauses gameplay.

A visible SDL run with audio and 4x resolution recorded a 23.4 ms mean update
interval, 34.9 ms p95, 36.9 ms p99 and 44.5 ms maximum over fields 1400–1900.
This is one local machine and route, not a promise of laptop performance or a
controlled measurement of each optimization's speedup.

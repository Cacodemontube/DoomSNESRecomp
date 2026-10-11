# v0.02

- Windows x64 and x86, Linux x64, and native macOS Intel and Apple Silicon releases.
- Fixed Ctrl+L mod-change resume crashes by saving the native CPU continuation
  and beam-driver state with the emulated machine.
- Fixed distorted status-bar faces and menu artwork after resume by preserving
  PPU graphics-upload settings and write latches. Paused resume also retains
  HUD counters, weapon artwork and the enhanced world view.
- Stabilized pause menus at startup and after IDCHOPPERS. Plain R no longer
  triggers the host renderer shortcut while entering cheats; Ctrl+R remains available.
- Fixed close enemy sprites breaking or duplicating across widescreen camera
  boundaries. Sprite discovery is merged and each native identity is projected once.
- Fixed Episode 2/3 skies reverting to Episode 1 at some angles or with mouselook.
- Removed brief native-resolution/aspect flashes during pause and automap handoffs.
- Reduced Super FX observer overhead and prevented simulation catch-up from
  starving presentation updates.
- Improved audio continuity across buffer-starvation recovery by preserving
  the native consumer's fade instead of replacing it abruptly with silence.
- Added native music-cheat handling and picture retention during track uploads,
  plus broader input, cheat, rendering, restart and audio diagnostics.
- Fifteen automated regression tests, renderer lifecycle checks and live
  running/paused mod-change tests cover the updated behavior.

Known limitations: the reported E1M1 music crackle is still unresolved; the
starvation fix does not establish that all crackling is gone. Enemy artwork and
simulation retain the original SNES game's discrete timing. Older incomplete
savestates lack required continuation/graphics state and are rejected; SRAM
saves remain compatible. Wall/level geometry projection was left unchanged
during the close-sprite and restart fixes.

Runtime archives include assets, default-disabled mod packages, licenses and
checksums. They contain no ROM, generated source, personal configuration or saves.

# v0.01

- Windows x64 and x86 runtime ZIPs with the `logo1.png` executable icon.
- Optional high-resolution 3D rendering and automap, adaptive widescreen,
  interpolated presentation, and transparent automap.
- Improved missing-wall and corner handling while preserving SNES texture alignment.
- In-game menus retain the high-resolution scene, widescreen white tint,
  frozen weapon and status bar.
- Disabled the host's keyboard P pause shortcut; Doom's pause remains available.
- Toggleable PC keyboard cheats, difficulty-independent episode selection,
  and modern keyboard and mouse controls.

Players supply their own verified Doom (USA) ROM. The ZIPs include no ROM,
generated source, or machine-specific settings. Enhancements are off by default.

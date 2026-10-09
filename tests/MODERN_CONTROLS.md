# Modern keyboard and mouse

The default-off `modern-controls` feature lives in Doom Display Enhancements.
It uses an activation plugin, not a presentation-only plugin: horizontal
mouse movement changes gameplay. The keyboard adapter replaces player 1's
keyboard pad word while preserving gamepads, scripts and developer input.
Turning the feature off restores `keybinds.ini` without rewriting that file.

W/S use native forward/reverse, A/D use native L/R strafing, Shift uses B
(run), E uses A (use), Enter uses Start, and left click uses Y (fire).
Those face buttons are the retail USA `useID8` layout. Opposing movement
keys cancel. Ctrl/Alt shortcuts remain available to the host. Escape releases
relative mouse capture; click recaptures it. Focus loss, host pause and
modal panels clear pending motion and release capture.

## Original mouse base

The conversion is checked against [DOOM-FX rlplayer.a](https://github.com/RandalLinden/DOOM-FX/blob/master/source/rlplayer.a),
especially CJY5200/CJY5300, and [rlplayer2.a](https://github.com/RandalLinden/DOOM-FX/blob/master/source/rlplayer2.a).
Retail horizontal mouse conversion yields 64 angle units per relative count.
The PC adapter accumulates relative counts and consumes them once at the
native player movement entry. This replaces the old last mouse packet's
velocity multiplied by FPSRatio, so PC motion does not depend on the
variable native rendering interval.

The original mover shares horizontal velocity between turning and strafing.
An opt-in GSU PC hook at retail `$0081c1` applies mouse yaw independently,
after death/falling checks and before native movement reads the player angle.
It resolves the same player-object pointer at `$018c` and angle offset 20
used by the retail code. Native keyboard movement, collision, run speed,
doors, weapons and pause remain the game's own routines. Signature checks
reject unsupported layouts. Private renderer replays inherit no gameplay hook.

## Classic vertical look

The world rasterizer shifts its horizon by at most 42 native rows. Wall and
portal boundaries, floor/ceiling rays, wall texture phase, sky and sprite
projection use the same offset. It renders newly exposed world pixels rather
than shifting a completed framebuffer and duplicating its edges. HUD and
weapon drawing occurs afterward at the original positions. Mouse up looks
up by default; invert-Y reverses it, and Home centers it.

This is a visual horizon shear, not true camera pitch or vertical weapon aim.
SNES Doom's shooting/auto-aim is retained. Classic vertical look can be
disabled independently of WASD and horizontal mouse turning.

## Validation

The release build and all 11 CTest tests pass. The input adapter test executes
the real Super FX core with deterministic SDL input sources, checking native
angle updates, concurrent strafing, one-time motion consumption, capture,
focus loss, suspension, reset, disabled-mode passthrough and ROM guards.
The resolution test covers native through 4x rendering at both pitch limits
and verifies untouched rows and padded buffers outside the world viewport.
The renderer lifecycle test also passes with the local USA ROM.

Actual gameplay checks at native 4:3 and 4x/32:9 applied 24 horizontal counts
while strafing, changing the player angle from `$4000` to `$3a00` and changing
its position. Looking up/down moved the horizon to +30/-34 rows, with zero
renderer replay failures. The widest composed captures were 2728x896.
Local results are under `modern-native-mouse-final` and
`modern-wide-mouse-verified` in `build-lag-evidence`.

The isolated disabled and enabled-without-mouse routes produced identical
CPU state traces and all 156 native dump artifacts, including sound, RAM,
CPU/GSU and native framebuffers. Local evidence is in
`build-lag-evidence/modern-default-disabled` and `modern-final-neutral`.

To repeat an actual-game diagnostic check of simultaneous strafe/turn,
look-up/look-down and composed screenshots, build with trace support and run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/validate_modern_controls.ps1 -RunLabel my-modern-check
powershell -NoProfile -ExecutionPolicy Bypass -File tests/validate_modern_controls.ps1 -RunLabel my-modern-wide-check -ResolutionScale 4 -Aspect 32:9
```

The harness copies the executable into an isolated evidence folder, uses a
dummy display and scripted inputs, and queues mouse motion through the
emulation thread. It does not drive a personal game or change player settings.
`game input_stats` and `game mouse_motion_test <x> <y>` are developer TCP
commands in trace builds; the test request requires enabled modern controls
and active gameplay. Normal desktop mouse input goes through the same motion
handler, with SDL capture/focus handling tested separately.

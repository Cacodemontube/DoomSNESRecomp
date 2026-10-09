# PC keyboard cheats

The default-disabled `doom.cheats` package registers the
`doom.cheats.keyboard` activation plugin. Keyboard input works independently
of modern controls and the selected controller input source. Codes are
case-insensitive; repeats, modifier shortcuts, focus loss and host panels do
not complete codes. Commands execute at the native player tick, rather than
changing game RAM from another thread.

`IDDQD` grants exactly 100 health on activation, prevents both direct damage
and health subtraction, and selects the original glowing-eye face animation.
Pickup grins cannot replace this face. It leaves the timed invulnerability
powerup and its palette alone. Disabling god mode keeps current health and
restores the appropriate native face, including a real invulnerability pickup.

`IDFA` and `IDKFA` set all eight weapon bits and 200 armor. Ammo capacities
are 200 bullets, 50 shells, 50 rockets and 300 cells, doubled if a backpack is
already owned. `IDFA` preserves keys; `IDKFA` grants all six key bits.

`IDCLIP` applies the native movement velocity without wall clipping, then
runs the original sector unlink, BSP lookup, sector relink and line triggers.
The player's floor and ceiling follow the destination sector. Enemy movement
and collision routines remain native.

`IDDT` first grants ComputerMap, then enables native green triangle drawing
for living enemy objects. The third use restores the previous map-powerup
state. Missiles, pickups and dead enemies are filtered. Map reveal resets with
native level initialization.

`IDCLEVxy` accepts the 22 levels included in SNES Doom. It requests the
native `$NN52` ExitLevel transition: black screen, new inventory, 100 health,
50 bullets, fist and pistol, no armor or keys. It preserves SkillLevel and
updates the native Level variable, so later exits use the normal progression,
including omitted-map skips and secret-map returns. It does not substitute a
different difficulty to bypass SNES episode restrictions.

The hooks target the verified USA retail ROM and check every hook's opcode
signature before installation. They are removed when the feature is disabled.
God mode and noclip are session toggles; they are not serialized as additional
save-state data. Granted inventory is native game state.

The native routines were checked against the original DOOM-FX sources:
[rlplayer2.a](https://github.com/RandalLinden/DOOM-FX/blob/master/source/rlplayer2.a),
[rlstatus.a](https://github.com/RandalLinden/DOOM-FX/blob/master/source/rlstatus.a),
[rlmove4.a](https://github.com/RandalLinden/DOOM-FX/blob/master/source/rlmove4.a),
[rlautomap.a](https://github.com/RandalLinden/DOOM-FX/blob/master/source/rlautomap.a),
and [rage.a](https://github.com/RandalLinden/DOOM-FX/blob/master/source/rage.a).
The retail RAM layout is verified through native execution rather than
assuming that every variable matches the published source revision.

## Validation

The release build and all 12 CTest tests pass. `doom_cheats_test` checks the
production SDL parser, opcode guards, inventory capacities, all warp codes,
reversible toggles, focus/suspension, and unsupported ROM rejection. With a
local USA ROM it executes native damage, health subtraction and face animation
in the real Super FX core, including damage after disabling god mode and the
glowing-eye face taking priority over a pickup grin. Without that ROM, the
test uses synthetic hook signatures and skips native-routine execution.

Running-game evidence is kept locally in `build-lag-evidence`:

- `cheats-gameplay-second`: all 22 warp destinations reached their correct
  native level indices, with pistol-start inventory at selected skill 2.
- `cheats-arrows-progression`: god face and health, both inventory cheats,
  native automap monster drawing and screenshot, wall noclip, and normal
  level progression after a warp. Normal movement stopped at Y=-2897;
  noclip passed the same wall to Y=-2842. A normal exit after `IDCLEV15`
  advanced to E1M7, skipping the absent E1M6.

Repeat the running-game checks with a trace-enabled build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/validate_cheats.ps1 -RunLabel my-cheats-check
```

Use `-SkipWarps` for the targeted automap/noclip check and E1M5 normal exit.
The full route also checks the normal return from E3M9 to E3M7. The harness
uses an isolated executable/config/mod state and a dummy display. Developer
TCP commands queue requests onto the emulation thread; they are trace-only.

Adding a second mod package also exposed a Windows build issue: the catalog
checker passed package IDs joined by `|` into nested `cmd.exe` commands.
`snesrecomp/runner/runner.cmake` now joins them with commas, and
`snes_check_mod_catalog.cmake` accepts commas and the old separator.

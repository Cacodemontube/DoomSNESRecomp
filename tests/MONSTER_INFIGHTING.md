# Optional monster infighting test build

Enable **Mods > Monster Infighting > Enable monster infighting**, then Play.
It is disabled by default and requires no PC WAD. Start a new level after
changing it so existing targets/projectiles belong to the selected rules.
The PC rotations and crosshair mods can be selected separately.

## Behavior

An injured monster targets the monster responsible for the hit. The native
SNES AI then handles sight, movement, aiming, melee, projectiles, pain and death.
Successful Zombieman, Shotgun Guy and Spiderdemon gunshots can hit an intervening
monster, including their own species. Player shots can take a monster's
attention back. Player projectiles retain player ownership even when reusing
a slot previously occupied by an enemy projectile.

Same-species protections requested for this mod:

| Attacker | Attack blocked against its own species |
| --- | --- |
| Imp | Imp fireball |
| Pinky | Melee bite |
| Lost Soul | Native SNES melee bite |
| Cacodemon | Cacodemon fireball |
| Baron of Hell | Baron fireball |
| Cyberdemon | Rocket direct hit and rocket splash |

Cross-species attacks remain damaging. Player rockets still damage Cyberdemons.
Lost Souls retain their SNES bite behavior. Native damage rolls, kill counts,
projectile collision/explosion animations and the renderer remain in use.

This adapts infighting to DOOM-FX rather than replacing its AI with the PC
engine. Native sight reacquisition and attack accuracy remain in effect;
missed enemy gunshots are still native misses, without a new PC scatter trace.
Barrel explosions retain their native damage and do not attribute retaliation
to the monster that initially shot the barrel. There is no PC retaliation
threshold timer; the native target/sight routines determine continued pursuit.

## Implementation and references

Randy Linden's [DOOM-FX source](https://github.com/RandalLinden/DOOM-FX)
was used to identify the native contracts in `rlmobjects3.a`, `rlenemies.a`,
`rlenemies5.a`, `rlenemies6.a`, `rlprojectiles.a`, `rlradius.a`, and `rlweapons3.a`.
The distinction between the shooter and its projectile, retaliation and
same-species missile policy were checked against id Software's
[P_DamageMobj](https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/p_inter.c)
and [missile collision code](https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/p_map.c).
The requested Cyberdemon and SNES melee exceptions are deliberate adaptations.

Eight bounded GSU hooks are installed only when enabled, after retail opcode
guards pass. The only world scan is at a successful enemy gunshot (100 slots
maximum). Existing rendering/interpolation optimizations are unchanged.
Ownership is stored outside native object records, whose projectile fields
are already occupied by velocities. It is serialized with saves and rollback.
Previous EX3 title saves load with empty ownership data; begin a fresh level
for complete attribution when enabling this mod. EX4 saves belong to this
new build; keep older builds and their saves in their original directories.

## Validation

- Production hooks passed 120 attack matrix cases using the retail GSU code.
- Checked successful gunfire interception, height/offset/dead/item exclusions,
  both native projectile birth tails, slot reuse, player aggro, Cyberdemon
  splash ownership after projectile reuse, and native health subtraction.
- Checked ownership save/load and rejected malformed saved pointers.
- All eight selected infighting, cheats, crosshair, resolution, video,
  projection, shading and presentation regression tests passed.
- Live E3M1 exercised projectile births, attack hits and same-species blocking.
- Live E1M3 exercised intercepted gunfire and monster retaliation.
- Disabled-mod live run had no installed infighting hooks or events.
- Desktop save/load succeeded with EX4 and reconstructed preceding EX3 data.

Evidence: `build-evidence/infighting/`. The separate ZIP contains no ROM, IWAD,
imported sprites, user configuration paths or save files. Previous builds,
including the original `build/DoomSNESRecomp.exe`, are preserved.

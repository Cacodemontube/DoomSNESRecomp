# Review build: separate projectile arrows and original-cadence vertical look

The current normal Release executable is `build-review-release/DoomSNESRecomp.exe`.
The previous review build, assets, mod state and configuration are preserved in
`build-review-release/rollback-20261012-222221`. Personal configuration, keybinds,
ROM selection and saves were preserved. This build has not been published.
Earlier review fixes remain included; see `tests/V004_FOLLOWUP.md` for that work.

## IDDT projectile arrows

Reviewed MISS.mp4. Two errors in the previous change caused the reported behavior:

- Native projectile tags are **64 through 68**: imp, cacodemon and baron fireballs,
  rockets and plasma. The previous 65-through-69 filter omitted imp fireballs and
  incorrectly admitted barrel explosions (69). Projectiles do not require enemy
  health; their active processing-list entry controls their lifetime.
- Filtering at native PC ed42 could reject an object before its advanced processing
  list cursor was saved. The next iteration then reread the same rejected object,
  consuming the remaining object count and omitting enemies later in the list.
  Filtering now occurs at ed45, after the original cursor-save instruction executes.
  The native arrow geometry, position and angle routines still draw each arrow.

Enhanced automap interpolation now matches each triangle edge by native object
pointer, object type and edge number. Previously it matched lines by their offset
from the end of the draw list, which could blend a newly inserted projectile with
another object's arrow. New arrows appear at their own coordinates; existing
arrows retain their own interpolation history when missiles appear or disappear.

All of this is conditional on the second IDDT mode. Ordinary map discovery and the
first IDDT mode retain their behavior. Native and enhanced map rendering share the
corrected native selection. No projectile or monster simulation was replaced.

## Vertical look

Reviewed look.mp4. Pitch previously came directly from the latest mouse event on
all host presentations, while original-mode world cameras advanced only when a
new native world scene was captured. Vertical motion therefore updated much more
frequently than walking and horizontal turning.

With interpolation disabled, pitch is now sampled at the same native world capture
boundary and held between captures. This follows the game's actual, variable world
cadence rather than imposing an arbitrary fixed FPS. With interpolation enabled,
vertical look continues using live mouse pitch. Recentring also uses the sampled
pitch until the next native capture; disabling the look option clears it normally.
The policy applies at native resolution and with high resolution/widescreen enabled.

## Validation

- All 15 CTests pass in diagnostic and normal Release builds.
- ROM-backed cheats and renderer lifecycle tests pass in both builds.
- The automap regression executes the retail GSU iterator with a rejected item,
  an empty slot, an imp fireball and an imp. It records player, fireball and imp as
  distinct arrow draws, verifies that the cursor advances, then removes the
  fireball and verifies that the imp remains. All five projectile tags and exclusion
  of barrel explosions are covered.
- Renderer tests insert and remove a projectile before an enemy in the arrow list.
  The projectile keeps its own coordinates and the enemy keeps its own history.
- Live original-cadence look: over 80 simulated fields, 16 native captures produced
  exactly 16 pitch changes; pitch remained held on the other 64 fields.
- The same live check passed with 4x resolution and 16:9 widescreen.
- Live interpolated look at 2x resolution: 80 pitch changes over the same 80 fields,
  preserving smooth vertical look.
- Live rocket-flight check at 2x resolution with interpolation: one separate native
  projectile arrow was captured while all six enemy arrows remained present. Enemy
  count stayed at six after the rocket disappeared. Evidence includes the captured
  map lines and screenshot in `build-lag-evidence/review005-projectiles7`.

Reproduction: `tests/validate_projectile_look.ps1` accepts `-Interpolated`,
`-ResolutionScale`, `-Aspect` and `-Projectiles`. Tests use isolated configuration
and a user-owned US ROM. No Japanese ROM was available for direct binary comparison;
the arrows use the original SNES drawing routine and native object coordinates.

Video extracts and build logs are in `build-evidence/review-005`; live traces and
screenshots are in `build-lag-evidence/review005-*`. These checks address the two
reported regressions; they do not establish a fix for the earlier Linux performance
or audio report on hardware that was not tested here.

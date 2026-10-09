# Unlocked episodes validation

Enable **Mods → Doom Unlocked Episodes → All episodes at every difficulty**.
The feature defaults to off and is independent of keyboard cheats.

The implementation follows the original DOOM-FX `useIMAGINEER` behavior:
remove the episode menu's difficulty limit and the difficulty checks that
terminate progression at episode boundaries. The selected skill and native
inventory, score, pistol-start and endgame handling remain in use.

Two retail instruction signatures guard four reversible in-memory byte
changes. The ROM file is never modified. Disabling the feature or shutting
down restores the original bytes. An unsupported signature changes nothing.

`doom_episodes_test` checks both guards, the full menu height, the progression
branch, exactly four changed bytes, repeated activation and complete restoration.
It uses the local USA ROM when available, otherwise synthetic signatures.

For diagnostic builds, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/validate_episodes.ps1 -RunLabel episodes-enabled
powershell -NoProfile -ExecutionPolicy Bypass -File tests/validate_episodes.ps1 -RunLabel episodes-disabled -Disabled
```

The harness uses an isolated copy of the executable, configuration and mod
catalog, with dummy SDL video/audio and TCP controls. It checks all five skill
menus and captures the episode selection screens. With the feature enabled it
starts Episode 3 at skill 0, warps to E1M8 and E2M8, and requests native normal
exits, checking E2M1 and E3M1 respectively and unchanged skill 0. This exercises
the native exit path without requiring a manual boss fight.

Evidence is written under `build-lag-evidence/<RunLabel>/`; personal settings
are not changed. The keyboard cheats mod is enabled solely for test warps.

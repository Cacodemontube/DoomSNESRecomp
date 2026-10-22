# PC enemy and projectile rotations

In **Mods**, select **PC Enemy and Projectile Rotations**, enable its feature,
and select your full Doom or Ultimate Doom `DOOM.WAD`. Press **Play** to import.
The default is disabled. Shareware Doom, Doom II, PWADs, missing rotations,
and malformed sprite posts are rejected with an error dialog.

Sprites export beside the executable under `assets/pc-rotations/<wad-id>/`.
The runtime reads these exported `.lmp` files into immutable decoded patches;
there is no disk access while rendering. Repeated launches verify and reuse
the cache. Different IWADs keep separate caches. A failed import leaves native
sprites active and reports the failure. The IWAD resource must remain selected
and available on subsequent launches. No game artwork is bundled in the build.

Included: Zombieman, shotgun guy, imp, pinky, lost soul, cacodemon, Baron of
Hell, Spiderdemon and Cyberdemon. Rockets cover both the player launcher and
Cyberdemon. `BAL7` covers the shared SNES Baron/BFG projectile; it preserves
the SNES colour map instead of substituting the PC BFG sprite. Death frames
and projectile explosions retain their native imagery.

The implementation uses Randy Linden's local DOOM-FX source as its reference:

- `source/rle.i`, `rloX`, `rloY`, `rloAngle`, `rloImage` and `rlqObj` establish
  native object fields and the visible-object identity.
- `source/rlvobjs.a` establishes native projection, world size and image lookup.
- `source/rlmobjects3.a` establishes projectile angle units (a 16-bit turn).
- `source/ImageList` supplies PC lump names and enemy animation order in
  `src/doom_rotation_frames.inc`. Projectile indices are adjusted against the
  retail USA ROM's image table: Baron/BFG flight is 218/219 and rocket flight
  is 223. Source-list ordinals 215/216/220 are retail explosion images.
  Retail Spiderdemon image 122 is also corrected to PC frame E: it mirrors
  image 121's front artwork, giving the native A/B/A/mirrored-B walk cycle.

The host selects an octant from the object-to-viewer bearing minus the native
facing angle, preserving native AI and animation. Mirrored WAD names supply
both directions with corrected offsets. Sprite pixels are quantized to the
native palette and then use the existing light, glow, invulnerability, damage,
fade and depth handling. The mod requests host sprite rasterization at native
resolution as well as higher resolutions, and bypasses the native-view async
shortcut while enabled.

Validation with a user-owned IWAD:

```powershell
./build-interpolation-release/doom_rotations_test.exe DOOM.WAD ./build-interpolation-release/rotation-test/sprites
./build-interpolation-release/doom_resolution_test.exe DOOM.WAD ./build-interpolation-release/rotation-capture-test/sprites
./build-interpolation-release/doom_renderer_state_test.exe 'Doom (USA).sfc'
ctest --test-dir build-interpolation-release --output-on-failure -R 'doom_(resolution|presentation|video|projection|shading)$'
```

The import test checks all supported frame groups and eight directions,
mirrors, transparent versus opaque black, exported cache reuse and malformed
input. The resolution test exercises native visible-object capture and column
sampling for every actor/projectile at all eight bearings. Full interactive
gameplay still needs a visual check of sprite placement and occlusion.

The previous packaged Jitter-Fix build and title source were preserved in
`dist/rollbacks/Before-PC-Rotations-20261016-152401/`. Extract its ZIP to a
separate folder to use the rollback.

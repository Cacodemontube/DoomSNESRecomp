# Rotation identity fixes (Issue1.mp4 / Issue2.mp4)

Two bugs allowed unrelated sprites to receive imported rotations:

1. Fixed objects such as barrels, torches and pickups use short records, not
   the movable-object animation layout. Reading `object+23` on these records
   reads other data; values resembling Cacodemon frames were accepted.
2. The released DOOM-FX `ImageList` has 229 entries; the USA ROM has 232.
   Enemy frame indices agree, but the retail projectile block is three images
   later. Source Baron-flight indices 215/216 actually identify retail
   Cacodemon explosions. Source rocket-flight index 220 identifies a retail
   Baron explosion.

The mod now requires a matching object type and an allowed frame for that
specific enemy/projectile. Fixed objects, unrequested projectile families,
death frames and explosion frames retain the original native sprite.
The visible object's already-resolved image pointer and dimensions must also
agree with the object's animation byte through the retail ROM image table.
A mismatch falls back to the native image instead of guessing.

Retail reference: ROM bytes at offset `0x46ce` implement the image lookup
through `$5a8000`, adding base `$81d0`; image headers resolve in bank `$5a`.
The flying Baron/BFG fireballs are 218/219, and the flying rocket is 223.
Cacodemon projectile frames 213–217, Baron explosions 220–222 and rocket
explosions 224–226 are never replaced. The source references are DOOM-FX
`rle.i`, `rlobjects.i`, `rlvobjs.a`, `objdata.a` and `ImageList`.

The regression test exercises 16,175 disallowed object/frame pairs through
actual sprite capture, including reuse of an existing sprite identity and
poisoned short-record bytes. It verifies that image, size, flip and native
selection survive. Positive tests cover all nine enemies and both requested
projectile groups in all eight directions. Tests run both with synthetic data
and with the user-owned USA SNES ROM, checking its lookup instruction bytes.
Import tests also verify that retail explosion indices have no replacement.

```powershell
./build-interpolation-release/doom_rotations_test.exe DOOM.WAD build-evidence/rotation-identity-fix/import/sprites
./build-interpolation-release/doom_resolution_test.exe DOOM.WAD build-evidence/rotation-identity-fix/capture/sprites 'Doom (USA).sfc'
./build-interpolation-release/doom_renderer_state_test.exe 'Doom (USA).sfc'
powershell -NoProfile -ExecutionPolicy Bypass -File tests/validate_graphics_crosshair.ps1 -RunLabel rotation-identity-e1m8 -Rotations -Crosshair -Warp 18 -Port 8741
```

These are bounded identity checks at sprite capture. Imported assets, texture
caching, parallel raster workers, world caching and native gameplay are
otherwise unchanged. Existing WAD exports can be reused.

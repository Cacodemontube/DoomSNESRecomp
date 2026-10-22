# Spiderdemon walking correction

Reviewed `spider.mp4`, the user-owned USA SNES ROM and Randy Linden's DOOM-FX
`ImageList`, `objdata3.a`, `rle.i`, `rltraceo2.a` and `rltraceo3.a`.

The retail walk sequence at ROM offset `0x1fdf36` is:

```
8 ticks: image 120
8 ticks: image 121
8 ticks: image 120
8 ticks: image 122
repeat
```

Retail image 122 has the same height and width as 121, with its entire strip
pointer table reversed. It is a mirrored second step. The released source's
ImageList labels it SPIDC1, but using PC C here selects the wrong walking
phase. The mod now maps 122 to PC E. The imported sequence becomes A/B/A/E:
front E1 and rear E5 are the mirrored B1/B5 poses supplied by SPIDB1E1 and
SPIDB5E5. Side views use E2/E8, E3/E7 and E4/E6, preserving the correct leg
phase from those directions too. No additional global flip is applied.

## Audit findings

The audit compared strip-pointer order for every pair of supported animation
frames in all nine enemy families against the retail ROM.

| Enemy | Finding / action |
| --- | --- |
| Spiderdemon | 121/122 are mirrored walking poses. Corrected PC C to E for 122. |
| Pinky | Native 66/68 mirror one another. Existing PC B/D poses are distinct and correct; retained. |
| Baron of Hell | Native 95/97 mirror one another. Existing PC A/C poses are distinct and correct; retained. |
| Zombieman, shotgun guy, imp, Cacodemon, lost soul, Cyberdemon | No additional directly mirrored strip-table pair or related mapping mismatch found in this audit. |

Pinky and Baron store their front/back PC poses separately; the Spiderdemon
uses shared mirrored lumps. Treating every native mirrored pair as an extra
flip would damage the existing PC mappings. The fix changes only the
Spiderdemon walking-frame mapping and the export cache revision.

This is an audit of the relevant animation/mirror mappings, not a claim that
every possible animation in every level has been visually exercised.

## Validation

- The native ROM test verifies the actual walk command bytes and all three
  mirrored strip-table pairs, and scans all nine supported enemy families.
- Import tests verify opposite rendered leg poses for Pinky, Baron and
  Spiderdemon at front/back views; the Spider pair additionally must share
  identical raw pixels with opposite flip flags and matching mirrored anchors.
- Spiderdemon side-on B/E artwork must differ, preventing a front-only fix.
- All eight rotation directions, 16,175 excluded object/frame pairs,
  projectile identity guards and renderer state checks pass.
- Six CTest checks pass: crosshair, resolution, presentation, video,
  projection and shading.

The importer automatically writes a new `v2-<wad-id>` cache with the newly
required E-direction lumps. Earlier exports are preserved. The selected WAD
must remain available as before. Rendering and simulation optimizations,
native AI, animation timing, projectile fixes and crosshair are retained.

# Close sprites and native enemy cadence, 2026-10-11

The replacement root videos last 28.35 and 22.93 seconds. The close enemies
in the second clip change proportions at the widescreen camera joins.

The sprite rasterizer previously chose a separate yawed camera for each
output column. Each camera treats a sprite as facing that camera: stitching
these billboards changes depth and vertical scale across a nearby enemy.
Walls already use shared geometry and were not changed in this fix.

Visibility cameras now discover sprites, which are deduplicated by native
object identity and projected once in the output camera. The centre camera's
native image, palette, flip and pixel anchor take precedence. Side-only
objects are transformed into that same camera. Wall depth still occludes
sprites; objects behind the output near plane are rejected.

Validation:

- All 14 release and diagnostic CTests pass, plus the ROM-backed renderer
  state check. Pixel regressions cover close sprites across rx=108 at scales
  1 through 4, native texture columns, nearer-wall occlusion, deduplication
  and near-plane rejection.
- Before/after E1M2 Nightmare captures show the close shotgun enemy retaining
  consistent proportions in the side region. Both runs end at field 3372,
  with zero replay failures. These simultaneous capture runs are visual
  correctness evidence, not performance measurements.
- `validate_close_sprites.ps1` preserves isolated configuration, captures
  and TCP results. It selects Nightmare through native menus, warps to E1M2,
  enables god mode, waits for an enemy to approach and turns across the join.
  It waits for long TCP steps to finish even when the command's five-second
  timeout expires; otherwise subsequent commands can interrupt a step.

Original animation comparison:

The unmodified USA ROM in stock Snes9x was explicitly set to skill 0 through
its menus; its SRAM dump confirms that selection. During movement, 27 world
picture changes were four fields apart and nine were five fields apart,
with one initial 12-field gap. This measures picture cadence, not a separate
enemy animation rate. Source `rlmobjects2.a` decrements `rloAnimCount` using
`FPSCount`, then selects discrete next images. `rlmobjects3.a` selects a
different velocity table for Nightmare. These native routines still run.

Enhanced presentation interpolates object positions and angles, not artwork.
It also preserves integer coordinates consumed by the original GSU. Some
animation stepping and close-object position quantization are therefore
expected beside a smooth camera. This does not establish that every jump in
the user's recording is native behavior or rule out presentation stalls.

Local evidence: `build-lag-evidence/close-enemy-audit/`,
`close-sprites-before-e1m2/`, and `close-sprites-fixed-e1m2/`.

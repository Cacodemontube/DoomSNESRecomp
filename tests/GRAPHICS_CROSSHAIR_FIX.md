# Graphics repair and auto-aim crosshair

The PC rotation mod introduced a full host raster pass at native resolution.
That path was using individual visibility cameras rather than the merged
geometry and edge recovery already used at higher resolutions. Walls omitted
by a native two-pixel visibility test could therefore expose old framebuffer
pixels or change at camera boundaries. Native-resolution rotation rendering
now uses the merged scene, recovered edges, ray bins and prepared plane data.
It also reuses the world cache between unchanged presentations.

The striped floor lighting came from copying a two-pixel geometry column's
first dither phase into its neighbour. The second pixel now receives the
opposite floor colour-map phase. Wall, sky and sprite coverage suppress that
alternate where appropriate. Geometry intersections and texture decoding still
happen once per pair. Lighting tests use the player's SNES ROM colour maps;
the original `rltracef3.a` / floor plotter and `rltracew4.a` remain the lighting
references. Native wall lighting itself is not given an invented dither.

Texture caches, ray bins, plane caches, parallel workers, interpolation and
the native execution optimizations remain in place. The full world cache now
also covers native-resolution rotations. No performance claim is made for
hardware unavailable in this workspace.

## Auto-aim Crosshair mod

Enable **Auto-aim Crosshair → Auto-aim lock indicator** in Mods. It defaults
to off and works independently of the rotation mod or widescreen. A lime
green `+` sits at the viewport centre. When the original targeting rules
predict an enemy, the plus moves to it and turns red. It is hidden during
menus, automap, fades and unsupported scenes.

Randy Linden's `rlweapons3.a` shows that `_RLFireTarget` builds the list when
the weapon fires; `_RLFireGun` / `_RLFireProjectile` then select the nearest
eligible visible target. There is no persistent pre-fire lock to read.
`DoomAimSelect` previews the same active/target flags, actual drawn flag,
screen span, distance, first-target tie rule and weapon restrictions without
running the engine or advancing RNG. A closer targetable barrel prevents a
false red enemy lock for hitscan. Melee has its original range and vertical
cutoffs. The real game still selects its actual target at the shot; moving
targets and the next native update can change that prediction.

## Validation

- Six CTest checks pass: crosshair, resolution, presentation, video,
  projection and shading.
- The renderer state test passes against the user-owned USA SNES ROM,
  including opposite floor phases inside a duplicated pair, green/red marker
  drawing, automap suppression and no replay or object-memory mutation.
- The targeting test covers nearest/ties, the exact horizontal bounds, hidden
  and dead objects, projectile versus barrel filtering and melee cutoffs.
- Release startup/turn smoke runs completed for the original `build`, previous
  rotation package and fixed package.
- Diagnostic E3M1 captures cover the starting doorway, opening the door,
  walking outside and eight turning views, with native sprites and then with
  rotations/crosshair enabled. No renderer failures were reported. The fixed
  outdoor run reported recovered edges. Evidence is under
  `build-evidence/graphics-fix/debug-outdoor2-{fixed,native}/`.

The previous executable in `C:/projects/DoomSNESRecomp/build/` is preserved.
The earlier packaged rotation build and rollback ZIPs are also preserved.

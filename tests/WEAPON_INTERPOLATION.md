# Weapon presentation

Smooth weapon movement follows the existing **Interpolated frame rate** setting.
Disabling it immediately uses the native weapon position. Normal weapons are
opaque; native partial invisibility retains translucency and expiry blinking.

The renderer validates the cartridge's original uncut weapon layout and rebuilds
its tiles from captured PPU VRAM, retaining each tile's own palette. Clipping the
assembled sprite to the world viewport preserves bottom tiles during bobbing and
muzzle flashes with palettes different from the weapon body. Only position is
interpolated; artwork changes immediately. The HUD and guest state are unchanged.

Validation on Doom (USA):

- Eight CTests and the ROM-backed renderer integration check passed.
- Disabling interpolation produced zero interpolated weapon presentations.
- All 156 native dump artifacts and CPU/WRAM traces matched the prior run.
- Capture comparisons showed zero changed pixels in the HUD or outside the weapon.
- Firing captures restored flashes in 30 frames, with up to 76 additional bright
  pixels. Native state dumps and traces remained identical.
- An isolated developer fixture verified native invisibility activation, expiry
  blinking, and the return to an opaque weapon.

Evidence is in build-lag-evidence: flash-verification.json,
weapon-solid-full-final/equivalence.json, weapon-stock-final, and
invisibility-fixture-complete/fixture-result.json. Capture runs check correctness,
not precision performance.
#ifndef DOOM_SKY_H
#define DOOM_SKY_H

#include <stdint.h>

enum { kDoomSky1Surface = 0xffff, kDoomSky2Surface = 0xfffe };

/* The Reality engine sky is a panorama drawn in screen coordinates, rather
 * than a world plane. Continue its native two-pixel columns into the margins.
 * rldrawf.a wraps the horizontal byte before halving it, then wraps the whole
 * 128x128 address after subtracting Y (including a column borrow at Y>=128). */
static inline unsigned DoomSkyRomOffset(uint16_t angle, int world_x,
                                        unsigned y, unsigned sky)
{
    const unsigned origin = (uint16_t)((uint16_t)(0 - angle) * 4u) >> 8;
    /* Each native low-detail PLOT pair uses its even starting X for both
     * pixels. Preserve that phase even when the angle's origin byte is odd,
     * and round negative odd margin coordinates down to their even pair. */
    const unsigned pair_x = (unsigned)world_x & ~1u;
    const unsigned column = ((origin + pair_x) & 255u) >> 1;
    return 0x8000u + (sky ? 0x4000u : 0) +
        ((column * 128u + 127u - y) & 0x3fffu);
}

#endif

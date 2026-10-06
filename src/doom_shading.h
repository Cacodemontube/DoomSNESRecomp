#ifndef DOOM_SHADING_H
#define DOOM_SHADING_H

#include "doom_projection.h"

/* Source-camera depth / common-camera depth. Lighting in the Reality engine
 * uses inverse projected depth, so a yawed pass must normalize that scale
 * before choosing its colour map. Geometry keeps its native source scale. */
static inline double DoomLightingScaleRatio(double source_x, double yaw)
{
    const double common_depth = cos(yaw) -
        (source_x - DOOM_VIEW_WIDTH / 2.0) / DOOM_FOCAL * sin(yaw);
    return common_depth > 0 ? 1 / common_depth : 1;
}

static inline unsigned DoomNormalizeLightingScale(unsigned integer,
                                                  unsigned fraction,
                                                  double ratio)
{
    const double scale = ((integer << 15) | (fraction & 0x7fff)) * ratio;
    return (unsigned)lround(fmax(0, fmin(0x7fffff, scale)));
}

/* Original solid-floor light calculation (rltracef3.a). The lookup table
 * stores an integer byte followed by a 0.15 fraction. Each floor also draws
 * the next darker colour map in alternating final-screen pixels. */
static inline unsigned DoomFloorLightRow(unsigned darkness,
                                         unsigned adjustment,
                                         unsigned packed_scale)
{
    const unsigned brightest = darkness > adjustment ? darkness - adjustment : 0;
    const unsigned darkest = 2 * brightest + 8 < 247 ? 2 * brightest + 8 : 247;
    int level = (int)brightest;
    if (packed_scale < 0x10000) {
        level = (int)darkest -
            (((int)(packed_scale & 0x7fff) *
              ((int)darkest - (int)brightest)) >> 15);
        if (level > (int)darkest) level = (int)darkest;
        if (level < (int)brightest) level = (int)brightest;
    }
    return (unsigned)level >> 3;
}

static inline unsigned DoomFloorDitherRow(unsigned row, int x, unsigned y)
{
    return row + (((unsigned)x ^ y) & 1 ? 0 : 1);
}

#endif

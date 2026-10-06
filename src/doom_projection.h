#ifndef DOOM_PROJECTION_H
#define DOOM_PROJECTION_H

#include <math.h>
#include <stdint.h>

/* The original Reality engine scales world X/Y by 128 / (depth * 1.25).
 * Its 216x144 picture has its projection centre at (108,72). */
#define DOOM_VIEW_WIDTH 216u
#define DOOM_VIEW_HEIGHT 144u
#define DOOM_FOCAL 102.4
#define DOOM_SIDE_YAW (3.14159265358979323846 / 4.0)

/* Reproject one ray from the common, flat output camera into a yawed source
 * camera. The vertical division is necessary: merely stitching rotated
 * pictures produces bent horizontal edges and changes apparent wall height. */
static inline void DoomProjectRay(double x, double y, double yaw,
                                  double *source_x, double *source_y)
{
    const double ray_x = x / DOOM_FOCAL;
    const double z = cos(yaw) + ray_x * sin(yaw);
    *source_x = DOOM_VIEW_WIDTH / 2.0 +
        DOOM_FOCAL * (ray_x * cos(yaw) - sin(yaw)) / z;
    *source_y = DOOM_VIEW_HEIGHT / 2.0 + y / z;
}

static inline uint16_t DoomInterpolateAngle(uint16_t previous, uint16_t current,
                                          double fraction)
{
    const int delta = (int16_t)(uint16_t)(current - previous);
    return (uint16_t)(previous + (int)lround(delta * fraction));
}

#endif

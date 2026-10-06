#include "../src/doom_shading.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Check failed: %s, line %u\n", #condition, __LINE__); \
        abort(); \
    } \
} while (0)

int main(void)
{
    /* A flat wall with fixed common-camera depth must retain one lighting
     * scale across both native joins and all supported extended columns. */
    for (int x = -322; x <= 322; x++) {
        if (x > -108 && x < 108) continue;
        const double yaw = x < 0 ? -DOOM_SIDE_YAW : DOOM_SIDE_YAW;
        double source_x, source_y;
        DoomProjectRay(x, 0, yaw, &source_x, &source_y);
        const double source_depth = 1000 *
            (cos(yaw) + x / DOOM_FOCAL * sin(yaw));
        const unsigned source_scale = (unsigned)lround(
            (128 / (1.25 * source_depth)) * 32768);
        const double ratio = DoomLightingScaleRatio(source_x, yaw);
        const unsigned normalized = DoomNormalizeLightingScale(0,
            source_scale, ratio);
        CHECK(abs((int)normalized - (int)lround(0.1024 * 32768)) <=
              (int)ceil(ratio));
        CHECK(fabs(source_depth / ratio - 1000) < 1e-9);
    }
    CHECK(DoomNormalizeLightingScale(0, 24576, 2) == 49152);
    CHECK(DoomNormalizeLightingScale(255, 32767, 10) == 0x7fffff);

    /* Original Doom floor lighting limits and muzzle/infravision adjustment.
     * Next-row dither belongs to the final picture's coordinates. */
    CHECK(DoomFloorLightRow(63, 0, 16384) == 12);
    CHECK(DoomFloorLightRow(63, 0, 0x10000) == 7);
    CHECK(DoomFloorLightRow(63, 63, 0) == 1);
    CHECK(DoomFloorLightRow(63, 255, 0x10000) == 0);
    CHECK(DoomFloorLightRow(200, 0, 0) == 30);
    for (unsigned y = 0; y < 144; y++) {
        CHECK(DoomFloorDitherRow(7, 107, y) != DoomFloorDitherRow(7, 108, y));
        CHECK(DoomFloorDitherRow(7, -109, y) == DoomFloorDitherRow(7, 107, y));
    }
    puts("Doom shading tests passed");
    return 0;
}

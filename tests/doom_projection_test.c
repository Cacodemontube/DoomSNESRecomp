#include "../src/doom_projection.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Check failed: %s, line %u\n", #condition, __LINE__); \
        abort(); \
    } \
} while (0)

static void Close(double actual, double expected)
{
    CHECK(fabs(actual - expected) < 1e-8);
}

int main(void)
{
    /* The unchanged centre must preserve the native projection exactly. */
    double sx, sy;
    DoomProjectRay(-83, -57, 0, &sx, &sy);
    Close(sx, 25);
    Close(sy, 15);

    /* Every additional ray through the maximum compositor width must have
     * usable source coverage. Round trips verify both horizontal and
     * vertical perspective, including the boundaries between cameras. */
    for (int x = -380; x <= 379; x++) {
        const double yaw = x < -108 ? -DOOM_SIDE_YAW :
                           x >= 108 ? DOOM_SIDE_YAW : 0;
        for (int y = -72; y < 72; y++) {
            DoomProjectRay(x, y, yaw, &sx, &sy);
            CHECK(sx >= 0 && sx < DOOM_VIEW_WIDTH);
            CHECK(sy >= 0 && sy < DOOM_VIEW_HEIGHT);
            const double rx = (sx - DOOM_VIEW_WIDTH / 2.0) / DOOM_FOCAL;
            const double ry = (sy - DOOM_VIEW_HEIGHT / 2.0) / DOOM_FOCAL;
            const double z = cos(yaw) - rx * sin(yaw);
            Close(DOOM_FOCAL * (rx * cos(yaw) + sin(yaw)) / z, x);
            Close(DOOM_FOCAL * ry / z, y);
        }
    }

    /* Angle zero is a wraparound, and interpolation must take the shortest
     * path through it in both turning directions. */
    CHECK(DoomInterpolateAngle(0xff00, 0x0100, 0.5) == 0);
    CHECK(DoomInterpolateAngle(0x0100, 0xff00, 0.5) == 0);
    CHECK(DoomInterpolateAngle(0x4000, 0x5000, 0) == 0x4000);
    CHECK(DoomInterpolateAngle(0x4000, 0x5000, 1) == 0x5000);
    puts("Doom projection tests passed");
    return 0;
}

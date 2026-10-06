#include "../src/doom_projection.h"
#include "../src/doom_sky.h"

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

    /* Sky extends the original panorama in screen coordinates. Its artwork
     * has the same row and two-pixel phase across both native joins, unlike
     * a reprojected source-camera mountain (the reported regression). */
    CHECK(DoomSkyRomOffset(0, -2, 0, 0) == 0xbfff);
    CHECK(DoomSkyRomOffset(0, -1, 0, 0) == 0xbfff);
    CHECK(DoomSkyRomOffset(0, 0, 0, 0) == 0x807f);
    CHECK(DoomSkyRomOffset(0, 1, 0, 0) == 0x807f);
    CHECK(DoomSkyRomOffset(0, 0, 127, 0) == 0x8000);
    CHECK(DoomSkyRomOffset(0, 0, 128, 0) == 0xbfff);
    CHECK(DoomSkyRomOffset(0, 0, 143, 0) == 0xbff0);
    CHECK(DoomSkyRomOffset(0x0040, -1, 35, 0) ==
          DoomSkyRomOffset(0x0040, -2, 35, 0));
    CHECK(DoomSkyRomOffset(0x0040, 0, 35, 0) ==
          DoomSkyRomOffset(0x0040, 1, 35, 0));
    CHECK(DoomSkyRomOffset(0xffc0, -1, 35, 0) ==
          DoomSkyRomOffset(0xffc0, -2, 35, 0));
    CHECK(DoomSkyRomOffset(0xffc0, 0, 35, 0) ==
          DoomSkyRomOffset(0xffc0, 1, 35, 0));
    const unsigned sky_rows[] = {0, 35, 72, 127, 128, 143};
    for (unsigned angle = 0; angle < 65536; angle += 64) {
        for (unsigned sky = 0; sky < 2; sky++) {
            for (int x = -272; x < 488; x++) {
                for (unsigned row = 0; row < sizeof(sky_rows) / sizeof(sky_rows[0]); row++) {
                    const unsigned y = sky_rows[row];
                    const unsigned address = DoomSkyRomOffset((uint16_t)angle,
                        x, y, sky);
                    const unsigned base = sky ? 0xc000 : 0x8000;
                    CHECK(address >= base && address < base + 0x4000);
                    CHECK(address == DoomSkyRomOffset((uint16_t)angle,
                        x + 256, y, sky));
                    CHECK(((address - base + y) & 127) == 127);
                }
            }
        }
        /* Native 216-pixel sky columns and immediately adjacent margins.
         * Only the native texture wrap can step the horizontal address. */
        CHECK(DoomSkyRomOffset((uint16_t)angle, -1, 35, 0) ==
              DoomSkyRomOffset((uint16_t)angle, -2, 35, 0));
        CHECK(DoomSkyRomOffset((uint16_t)angle, 215, 35, 0) ==
              DoomSkyRomOffset((uint16_t)angle, 214, 35, 0));
    }
    puts("Doom projection tests passed");
    return 0;
}

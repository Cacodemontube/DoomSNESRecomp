#include "doom_video.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep checks active in Release builds. */
#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
    exit(1); \
} } while (0)

static void check_viewport(DoomViewport v, int expected_width) {
    CHECK(v.width == expected_width);
    CHECK(v.width == DOOM_STOCK_WIDTH + 2 * v.extra);
    CHECK(v.width >= DOOM_STOCK_WIDTH && v.width <= DOOM_MAX_WIDTH);
    CHECK(!(v.width & 1));
    CHECK(v.aspect >= 4.0 / 3.0 && v.aspect <= 32.0 / 9.0);
    /* The guest center and both borders keep their original coordinates
     * after adding the same centered integer offset. */
    CHECK(128 + v.extra == v.width / 2);
    CHECK(v.extra >= 0 && v.extra + DOOM_STOCK_WIDTH <= v.width);
}

static void geometry_tests(void) {
    DoomVideoSettings s;
    DoomVideoDefaults(&s);
    CHECK(!s.widescreen && !s.fps_enabled && s.fps == 0 && s.interpolate);
    CHECK(s.aspect == DOOM_ASPECT_FIT);
    check_viewport(DoomCalculateViewport(&s, 1920, 1080), 256);
    s.widescreen = true;
    const int widths[] = {256, 342, 448, 682};
    for (int i = DOOM_ASPECT_STOCK; i <= DOOM_ASPECT_32_9; ++i) {
        s.aspect = (DoomAspect)i;
        check_viewport(DoomCalculateViewport(&s, 600, 900), widths[i]);
    }
    s.aspect = DOOM_ASPECT_FIT;
    check_viewport(DoomCalculateViewport(&s, 1920, 1080), 342);
    check_viewport(DoomCalculateViewport(&s, 2560, 1080), 456);
    check_viewport(DoomCalculateViewport(&s, 5120, 1440), 682);
    check_viewport(DoomCalculateViewport(&s, 600, 900), 256);
    check_viewport(DoomCalculateViewport(&s, 0, 0), 256);
    check_viewport(DoomCalculateViewport(&s, -1, 1080), 256);
    check_viewport(DoomCalculateViewport(&s, INT_MAX, 1), 682);
    check_viewport(DoomCalculateViewport(&s, 1, INT_MAX), 256);
    s.aspect = (DoomAspect)99;
    check_viewport(DoomCalculateViewport(&s, 1920, 1080), 256);
    check_viewport(DoomCalculateViewport(NULL, 1920, 1080), 256);

    s.aspect = DOOM_ASPECT_32_9;
    DoomRect r = DoomDestination(DoomCalculateViewport(&s, 1920, 1080), 1920, 1080);
    CHECK(r.x == 0 && r.y == 270 && r.w == 1920 && r.h == 540);
    s.aspect = DOOM_ASPECT_16_9;
    r = DoomDestination(DoomCalculateViewport(&s, 1920, 1080), 1920, 1080);
    CHECK(r.x == 0 && r.y == 0 && r.w == 1920 && r.h == 1080);
    DoomVideoStock(&s);
    r = DoomDestination(DoomCalculateViewport(&s, 1920, 1080), 1920, 1080);
    CHECK(r.x == 240 && r.y == 0 && r.w == 1440 && r.h == 1080);
    r = DoomDestination((DoomViewport){.aspect = NAN}, 1920, 1080);
    CHECK(r.x == 240 && r.w == 1440);
    r = DoomDestination((DoomViewport){.aspect = INFINITY}, 1, 1);
    CHECK(r.w == 1 && r.h == 1);
    r = DoomDestination((DoomViewport){.aspect = 4.0 / 3.0}, 0, 1080);
    CHECK(r.x == 0 && r.y == 0 && r.w == 0 && r.h == 0);
    r = DoomDestination((DoomViewport){.aspect = 32.0 / 9.0}, INT_MAX, INT_MAX);
    CHECK(r.w == INT_MAX && r.h > 0 && r.h <= INT_MAX);
    CHECK(DoomWindowBaseWidth(256) == 320);
    CHECK(DoomWindowBaseWidth(448) == 560);
    CHECK(DoomWindowBaseWidth(INT_MAX) == 320);
}

static void parsing_tests(void) {
    for (int i = 0; i < DOOM_ASPECT_COUNT; ++i) {
        DoomAspect aspect = DOOM_ASPECT_STOCK;
        CHECK(DoomParseAspect(DoomAspectName((DoomAspect)i), &aspect));
        CHECK(aspect == (DoomAspect)i);
    }
    DoomAspect aspect = DOOM_ASPECT_FIT;
    CHECK(!DoomParseAspect("21:9junk", &aspect) && aspect == DOOM_ASPECT_FIT);
    CHECK(!DoomParseAspect(NULL, &aspect));
    CHECK(!DoomParseAspect("Fit", NULL));
    CHECK(!strcmp(DoomAspectName((DoomAspect)-1), "4:3"));

    const unsigned rates[] = {0, 60, 90, 120, 144, 165, 240, 360};
    const char *names[] = {"Auto", "60", "90", "120", "144", "165", "240", "360"};
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        unsigned fps = 999;
        bool enabled = false;
        CHECK(DoomValidFps(rates[i]));
        CHECK(DoomParseFrameRate(names[i], &fps, &enabled));
        CHECK(fps == rates[i] && enabled);
    }
    unsigned fps = 144;
    bool enabled = true;
    CHECK(DoomParseFrameRate("Original", &fps, &enabled));
    CHECK(fps == 0 && !enabled);
    const char *invalid[] = {"", "0", "30", "61", "-60", "+60", "060", "60.0", "60fps", "60 ", "99999999999999999999"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        fps = 144; enabled = true;
        CHECK(!DoomParseFrameRate(invalid[i], &fps, &enabled));
        CHECK(fps == 144 && enabled);
    }
    CHECK(!DoomParseFps(NULL, &fps));
    CHECK(!DoomParseFps("Auto", NULL));
    CHECK(!DoomParseFrameRate(NULL, &fps, &enabled));
    CHECK(!DoomParseFrameRate("Original", NULL, &enabled));
    CHECK(!DoomParseFrameRate("Original", &fps, NULL));
    CHECK(!DoomValidFps(UINT_MAX));
}

static void presentation_tests(void) {
    DoomVideoSettings s;
    DoomVideoDefaults(&s);
    CHECK(DoomPresentationHz(&s, 165) == 0);
    CHECK(DoomPresentationAlpha(&s, 0.5) == 1);
    s.fps_enabled = true;
    CHECK(DoomPresentationHz(&s, 0) == 60); /* pre-window decoupling probe */
    CHECK(DoomPresentationHz(&s, NAN) == 60);
    CHECK(DoomPresentationHz(&s, INFINITY) == 60);
    CHECK(DoomPresentationHz(&s, -1) == 60);
    CHECK(DoomPresentationHz(&s, 59.94) == 59.94);
    CHECK(DoomPresentationHz(&s, 500) == 360);
    s.fps = 144;
    CHECK(DoomPresentationHz(&s, 60) == 144);
    CHECK(DoomPresentationHz(&s, NAN) == 144);
    s.fps = UINT_MAX;
    CHECK(DoomPresentationHz(&s, 120) == 120);
    CHECK(DoomPresentationAlpha(&s, 0.5) == 0.5);
    CHECK(DoomPresentationAlpha(&s, -1) == 0);
    CHECK(DoomPresentationAlpha(&s, 2) == 1);
    CHECK(DoomPresentationAlpha(&s, NAN) == 1);
    CHECK(DoomPresentationAlpha(&s, INFINITY) == 1);
    s.interpolate = false;
    CHECK(DoomPresentationAlpha(&s, 0.5) == 1);
    /* The aspect and presentation rate toggles are independent. */
    s.widescreen = true;
    s.aspect = DOOM_ASPECT_21_9;
    check_viewport(DoomCalculateViewport(&s, 1920, 1080), 448);
    s.fps_enabled = false;
    CHECK(DoomPresentationHz(&s, 165) == 0);
    check_viewport(DoomCalculateViewport(&s, 1920, 1080), 448);
    s.fps_enabled = true;
    s.widescreen = false;
    CHECK(DoomPresentationHz(&s, 120) == 120);
    check_viewport(DoomCalculateViewport(&s, 1920, 1080), 256);
    CHECK(DoomPresentationHz(NULL, 165) == 0);
    CHECK(DoomPresentationAlpha(NULL, 0.5) == 1);
}

int main(void) {
    geometry_tests();
    parsing_tests();
    presentation_tests();
    puts("Doom video: adaptive geometry, strict options, and independent presentation policy passed");
    return 0;
}

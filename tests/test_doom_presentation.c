#include "doom_presentation.h"
#include "doom_renderer.h"
#include "doom_video.h"

#define SDL_MAIN_HANDLED 1
#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "desktop/config.h"
#include "mod_runtime.h"
#include "snes/cart.h"
#include "snes/ppu.h"
#include "snes/superfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
    exit(1); \
} } while (0)

Config g_config;
static Snes machine;
static Cart cart;
static SuperFx fx;
static Ppu ppu;
Snes *g_snes = &machine;
Ppu *g_ppu = &ppu;

static bool wide_enabled, fps_enabled;
static const char *aspect_option = "Fit", *fps_option = "Auto";
static unsigned registered_plugins, registered_reset, configure_calls;
static unsigned reset_calls, begin_calls, end_calls, draw_calls;
static bool configured_wide, configured_interpolation;
static unsigned configured_width, ended_number;
static float drawn_alpha;
static SNESModActivationCallback plugin_callbacks[2], reset_callback;

/* Exercise the production adapter against the actual framework contracts.
 * The runtime itself is replaced only at its public query/registration API. */
int snes_mod_register_presentation_plugin(const char *id,
                                          SNESModActivationCallback callback) {
    CHECK(callback);
    unsigned index;
    if (!strcmp(id, "doom.presentation.widescreen")) index = 0;
    else if (!strcmp(id, "doom.presentation.fps")) index = 1;
    else { CHECK(0 && "unexpected presentation plugin id"); return 0; }
    CHECK(!plugin_callbacks[index]);
    plugin_callbacks[index] = callback;
    registered_plugins++;
    return 1;
}

int snes_mod_register_reset_callback(SNESModActivationCallback callback) {
    CHECK(callback && !reset_callback);
    reset_callback = callback;
    registered_reset++;
    return 1;
}

int snes_mod_runtime_feature_enabled_c(const char *package, const char *feature) {
    CHECK(package && !strcmp(package, "doom.presentation"));
    CHECK(feature);
    if (!strcmp(feature, "widescreen")) return wide_enabled;
    CHECK(!strcmp(feature, "presentation-fps"));
    return fps_enabled;
}

int snes_mod_runtime_feature_option_value_c(const char *package,
    const char *feature, const char *option, char *out, uint32_t capacity) {
    CHECK(package && !strcmp(package, "doom.presentation"));
    CHECK(feature && option && out);
    const char *value;
    if (!strcmp(feature, "widescreen")) {
        CHECK(!strcmp(option, "aspect"));
        value = aspect_option;
    } else {
        CHECK(!strcmp(feature, "presentation-fps") && !strcmp(option, "fps"));
        value = fps_option;
    }
    CHECK(capacity > strlen(value));
    strcpy(out, value);
    return 1;
}

void DoomRendererConfigure(SuperFx *core, bool wide, bool interpolation,
                           unsigned width) {
    CHECK(core == &fx);
    configured_wide = wide;
    configured_interpolation = interpolation;
    configured_width = width;
    configure_calls++;
}

void DoomRendererPreparePpu(Ppu *source) {
    CHECK(source == g_ppu);
    begin_calls++;
}

void DoomRendererEndSimFrame(unsigned number) {
    ended_number = number;
    end_calls++;
}

void DoomRendererReset(void) { reset_calls++; }

void DoomRendererGetStats(DoomRendererStats *out) {
    CHECK(out);
    *out = (DoomRendererStats){0};
}

enum { GUARD_BYTES = 64, GUARD_VALUE = 0xa5, ROW_PADDING_PIXELS = 7 };
static const uint8_t *expected_field;
static unsigned expected_width;
static size_t expected_pitch;

static void CheckSeed(const uint8_t *dst, size_t pitch, unsigned width,
                      unsigned height) {
    CHECK(expected_field && width == expected_width && pitch == expected_pitch);
    CHECK(height == DOOM_HEIGHT);
    const unsigned extra = (width - DOOM_STOCK_WIDTH) / 2;
    for (unsigned y = 0; y < height; ++y) {
        const uint8_t *row = dst + y * pitch;
        CHECK(!memcmp(row + extra * 4,
                       expected_field + y * DOOM_STOCK_WIDTH * 4,
                       DOOM_STOCK_WIDTH * 4));
        for (unsigned x = 0; x < extra * 4; ++x) CHECK(row[x] == 0);
        for (unsigned x = (extra + DOOM_STOCK_WIDTH) * 4; x < width * 4; ++x)
            CHECK(row[x] == 0);
        for (size_t x = width * 4; x < pitch; ++x) CHECK(row[x] == GUARD_VALUE);
    }
}

bool DoomRendererDraw(Ppu *source, uint8_t *dst, size_t pitch,
                      unsigned width, unsigned height, float alpha) {
    CHECK(source == g_ppu);
    /* Native pixels must already exist before the renderer overlays world
     * geometry; returning false must retain that centered fallback. */
    CheckSeed(dst, pitch, width, height);
    drawn_alpha = alpha;
    draw_calls++;
    return false;
}

bool DoomRendererDrawWeapon(Ppu *source, uint8_t *dst, size_t pitch,
                            unsigned width, unsigned height, float alpha,
                            bool world_redrawn) {
    CHECK(source == g_ppu && !world_redrawn);
    CheckSeed(dst, pitch, width, height);
    CHECK(alpha >= 0 && alpha <= 1);
    return true;
}

static void SetOptions(bool wide, const char *aspect, bool fps, const char *rate) {
    wide_enabled = wide;
    fps_enabled = fps;
    aspect_option = aspect;
    fps_option = rate;
    /* Both ordinary startup activation and its reset callback use the same
     * selection queries; disabling either feature restores original policy. */
    reset_callback();
    if (wide) plugin_callbacks[0]();
    if (fps) plugin_callbacks[1]();
}

static void settings_tests(void) {
    CHECK(registered_plugins == 2 && registered_reset == 1);
    int width, height;
    SetOptions(false, "21:9", false, "144");
    CHECK(DoomPresentationRate(165) == 0);
    CHECK(!DoomPresentationKeepDebt());
    DoomPresentationPrepare(1920, 1080, &width, &height);
    CHECK(width == 256 && height == 224 && configured_width == 256);
    CHECK(!configured_wide && !configured_interpolation);

    SetOptions(false, "21:9", true, "120");
    CHECK(DoomPresentationRate(0) == 120); /* Before a window exists. */
    CHECK(DoomPresentationKeepDebt());
    DoomPresentationPrepare(1920, 1080, &width, &height);
    CHECK(width == 256 && !configured_wide && configured_interpolation);

    SetOptions(true, "21:9", false, "120");
    CHECK(DoomPresentationRate(165) == 0);
    CHECK(DoomPresentationWindowWidth(256) == 560);
    CHECK(DoomPresentationKeepDebt());
    DoomPresentationPrepare(1920, 1080, &width, &height);
    CHECK(width == 448 && configured_wide && !configured_interpolation);

    SetOptions(true, "Fit", true, "Auto");
    CHECK(DoomPresentationRate(0) == 60);
    CHECK(DoomPresentationRate(NAN) == 60);
    CHECK(DoomPresentationRate(165) == 165);
    CHECK(DoomPresentationRate(500) == 360);
    CHECK(DoomPresentationWindowWidth(256) == 320);
    CHECK(DoomPresentationWindowWidth(448) == 560); /* Resize/scale regression. */
    DoomPresentationPrepare(1920, 1080, &width, &height);
    CHECK(width == 342 && configured_wide && configured_interpolation);
    DoomPresentationBeforeFrame();
    CHECK(configured_width == 342);
    CHECK(configure_calls >= 5);

    DoomPresentationBegin(123);
    DoomPresentationEnd(NULL, 123);
    CHECK(begin_calls == 1 && end_calls == 1 && ended_number == 123);
    DoomPresentationReset();
    CHECK(reset_calls == 1);

    SnesDisplayViewport rectangle;
    DoomPresentationViewport(342, 224, 1920, 1080, &rectangle);
    CHECK(rectangle.x == 0 && rectangle.y == 0 && rectangle.width == 1920 &&
          rectangle.height == 1080);
    SetOptions(false, "Fit", false, "Auto");
    g_config.display_aspect = kSnesDisplayAspect_SquarePixels8x7;
    g_config.ignore_aspect_ratio = false;
    DoomPresentationViewport(256, 224, 1920, 1080, &rectangle);
    CHECK(rectangle.width == 1234 && rectangle.height == 1080);
    g_config.ignore_aspect_ratio = true;
    DoomPresentationViewport(256, 224, 1920, 1080, &rectangle);
    CHECK(rectangle.width == 1920 && rectangle.height == 1080);
    g_config.ignore_aspect_ratio = false;
}

static void fallback_test(unsigned width, bool wide, bool fps, double alpha) {
    const size_t pitch = (width + ROW_PADDING_PIXELS) * 4;
    const size_t bytes = pitch * DOOM_HEIGHT;
    uint8_t *guarded = malloc(bytes + GUARD_BYTES * 2);
    uint32_t *field = malloc(DOOM_STOCK_WIDTH * DOOM_HEIGHT * sizeof(*field));
    CHECK(guarded && field);
    memset(guarded, GUARD_VALUE, bytes + GUARD_BYTES * 2);
    for (unsigned y = 0; y < DOOM_HEIGHT; ++y)
        for (unsigned x = 0; x < DOOM_STOCK_WIDTH; ++x)
            field[y * DOOM_STOCK_WIDTH + x] = 0x00a00000u | (y << 8) | x;
    uint8_t *dst = guarded + GUARD_BYTES;
    expected_field = (const uint8_t *)field;
    expected_width = width;
    expected_pitch = pitch;
    SetOptions(wide, "Fit", fps, "120");
    DoomPresentationBeforeFrame();
    const unsigned before = draw_calls;
    CHECK(DoomPresentationDraw(dst, pitch, (const uint8_t *)field,
                               (int)width, DOOM_HEIGHT, alpha) == 1);
    if (wide || fps) {
        CHECK(draw_calls == before + 1);
        CHECK(drawn_alpha == (float)(fps ? alpha : 1));
        CheckSeed(dst, pitch, width, DOOM_HEIGHT);
    } else {
        CHECK(draw_calls == before);
        CheckSeed(dst, pitch, width, DOOM_HEIGHT);
    }
    for (unsigned i = 0; i < GUARD_BYTES; ++i) {
        CHECK(guarded[i] == GUARD_VALUE);
        CHECK(guarded[GUARD_BYTES + bytes + i] == GUARD_VALUE);
    }
    expected_field = NULL;
    free(field);
    free(guarded);
}

int main(void) {
    machine.cart = &cart;
    cart.superfx = &fx;
    settings_tests();
    fallback_test(256, false, false, 0.25);
    fallback_test(256, false, true, 0.25);
    fallback_test(342, true, false, 0.25);
    fallback_test(448, true, true, 0.25);
    fallback_test(682, true, true, 0.75);
    puts("Doom presentation: mod queries, startup/resize policy, and guarded fallback passed");
    return 0;
}

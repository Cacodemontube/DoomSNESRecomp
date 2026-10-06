#include "doom_presentation.h"
#include "doom_renderer.h"
#include "doom_video.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "desktop/config.h"
#include "mod_runtime.h"
#include "snes/cart.h"

#include <string.h>

static DoomVideoSettings settings;
static DoomViewport viewport = {256, 0, 4.0 / 3.0};

/* Read the selection before window creation, too: the host probes the rate
 * before plugin activation to select its pacing and VSync policy. The normal
 * package provider remains responsible for editing and persisting options. */
static void ReadSettings(void) {
    char value[32];
    DoomVideoDefaults(&settings);
    settings.widescreen = snes_mod_runtime_feature_enabled_c(
        "doom.presentation", "widescreen") != 0;
    settings.fps_enabled = snes_mod_runtime_feature_enabled_c(
        "doom.presentation", "presentation-fps") != 0;
    if (snes_mod_runtime_feature_option_value_c("doom.presentation", "widescreen",
                                               "aspect", value, sizeof(value)))
        DoomParseAspect(value, &settings.aspect);
    if (snes_mod_runtime_feature_option_value_c("doom.presentation", "presentation-fps",
                                               "fps", value, sizeof(value)))
        DoomParseFps(value, &settings.fps);
}

SNES_MOD_CONSTRUCTOR(RegisterDoomPresentation) {
    snes_mod_register_presentation_plugin("doom.presentation.widescreen", ReadSettings);
    snes_mod_register_presentation_plugin("doom.presentation.fps", ReadSettings);
    snes_mod_register_reset_callback(ReadSettings);
}

void DoomPresentationReset(void) {
    DoomRendererReset();
}

static void ConfigureRenderer(void) {
    if (g_snes && g_snes->cart)
        DoomRendererConfigure(g_snes->cart->superfx, settings.widescreen,
                              settings.fps_enabled && settings.interpolate,
                              (unsigned)viewport.width);
}

void DoomPresentationBeforeFrame(void) {
    ReadSettings();
    ConfigureRenderer();
}

void DoomPresentationPrepare(int drawable_w, int drawable_h, int *width, int *height) {
    ReadSettings();
    viewport = DoomCalculateViewport(&settings, drawable_w, drawable_h);
    *width = viewport.width;
    *height = DOOM_HEIGHT;
    ConfigureRenderer();
}

void DoomPresentationBegin(unsigned number) {
    (void)number;
    DoomRendererPreparePpu(g_ppu);
}

void DoomPresentationEnd(const uint8_t *field, unsigned number) {
    (void)field;
    DoomRendererEndSimFrame(number);
    if (number % 120 == 0 && getenv("DOOM_RENDER_STATS")) {
        DoomRendererStats stats;
        DoomRendererGetStats(&stats);
        fprintf(stderr, "[doom-render] field=%u supported=%d captured=%llu "
                "passes=%llu cached=%llu failures=%llu interval=%u\n",
                number, stats.supported, (unsigned long long)stats.captures,
                (unsigned long long)stats.camera_passes,
                (unsigned long long)stats.cache_hits,
                (unsigned long long)stats.failures, stats.snapshot_interval);
    }
}

int DoomPresentationDraw(uint8_t *dst, size_t pitch, const uint8_t *field,
                         int width, int height, double alpha) {
    if (!settings.widescreen && !settings.fps_enabled) return 0;
    /* The stock field is always 256 wide. The generic copy fallback uses
     * output width as source stride, which is inappropriate for a custom
     * compositor. Menus and unsupported scenes stay centered and unstretched. */
    if (!dst || !field || width < DOOM_STOCK_WIDTH || height != DOOM_HEIGHT ||
        pitch < (size_t)width * 4) return 0;
    const int extra = (width - DOOM_STOCK_WIDTH) / 2;
    for (int y = 0; y < height; ++y) {
        uint8_t *row = dst + (size_t)y * pitch;
        memset(row, 0, (size_t)width * 4);
        memcpy(row + (size_t)extra * 4, field + (size_t)y * DOOM_STOCK_WIDTH * 4,
               DOOM_STOCK_WIDTH * 4);
    }
    DoomRendererDraw(g_ppu, dst, pitch, (unsigned)width, (unsigned)height,
                     (float)DoomPresentationAlpha(&settings, alpha));
    return 1;
}

double DoomPresentationRate(double refresh) {
    ReadSettings();
    return DoomPresentationHz(&settings, refresh);
}

int DoomPresentationKeepDebt(void) {
    /* A costly camera pass may miss a deadline. Keep simulation on its
     * original clock and catch up through cached presents, as F-Zero does,
     * instead of permanently turning each missed deadline into slow motion. */
    return settings.widescreen || settings.fps_enabled;
}

int DoomPresentationWindowWidth(int width) {
    ReadSettings();
    /* Before a drawable exists, fixed ratios should still open at their
     * chosen shape. Fit starts at the original 4:3 window until resized. */
    DoomViewport initial = DoomCalculateViewport(&settings, 0, 0);
    return DoomWindowBaseWidth(settings.widescreen && settings.aspect != DOOM_ASPECT_FIT
                              ? initial.width : width);
}

void DoomPresentationViewport(int width, int height, int drawable_w, int drawable_h,
                             SnesDisplayViewport *out) {
    if (!settings.widescreen) {
        SnesDisplayAspect_ComputeViewport(width, height, drawable_w, drawable_h,
            SnesDisplayAspect_Clamp(g_config.display_aspect),
            g_config.ignore_aspect_ratio, false, out);
        return;
    }
    DoomRect rect = DoomDestination(viewport, drawable_w, drawable_h);
    *out = (SnesDisplayViewport){rect.x, rect.y, rect.w, rect.h};
}

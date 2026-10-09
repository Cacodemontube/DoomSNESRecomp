#include "doom_video.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static const char *const aspect_names[] = {
    "4:3", "16:9", "21:9", "32:9", "Fit"
};
static const unsigned fps_values[] = {0, 60, 90, 120, 144, 165, 240, 360};
static const char *const fps_names[] = {
    "Auto", "60", "90", "120", "144", "165", "240", "360"
};

void DoomVideoDefaults(DoomVideoSettings *settings) {
    if (settings)
        *settings = (DoomVideoSettings){.aspect = DOOM_ASPECT_FIT,
                                       .interpolate = true,
                                       .resolution_scale = 2};
}

void DoomVideoStock(DoomVideoSettings *settings) {
    if (settings) *settings = (DoomVideoSettings){.aspect = DOOM_ASPECT_STOCK};
}

const char *DoomAspectName(DoomAspect aspect) {
    return aspect >= DOOM_ASPECT_STOCK && aspect < DOOM_ASPECT_COUNT
        ? aspect_names[aspect] : aspect_names[DOOM_ASPECT_STOCK];
}

bool DoomParseAspect(const char *text, DoomAspect *aspect) {
    if (!text || !aspect) return false;
    for (int i = 0; i < DOOM_ASPECT_COUNT; ++i) {
        if (!strcmp(text, aspect_names[i])) {
            *aspect = (DoomAspect)i;
            return true;
        }
    }
    return false;
}

bool DoomValidFps(unsigned fps) {
    for (size_t i = 0; i < sizeof(fps_values) / sizeof(fps_values[0]); ++i)
        if (fps == fps_values[i]) return true;
    return false;
}

bool DoomParseFps(const char *text, unsigned *fps) {
    if (!text || !fps) return false;
    for (size_t i = 0; i < sizeof(fps_names) / sizeof(fps_names[0]); ++i) {
        if (!strcmp(text, fps_names[i])) {
            *fps = fps_values[i];
            return true;
        }
    }
    return false;
}

bool DoomParseFrameRate(const char *text, unsigned *fps, bool *enabled) {
    if (!text || !fps || !enabled) return false;
    unsigned parsed;
    if (!strcmp(text, "Original")) {
        *fps = 0;
        *enabled = false;
        return true;
    }
    if (!DoomParseFps(text, &parsed)) return false;
    *fps = parsed;
    *enabled = true;
    return true;
}

DoomViewport DoomCalculateViewport(const DoomVideoSettings *settings,
                                   int drawable_width, int drawable_height) {
    double aspect = 4.0 / 3.0;
    if (settings && settings->widescreen) {
        switch (settings->aspect) {
        case DOOM_ASPECT_16_9: aspect = 16.0 / 9.0; break;
        case DOOM_ASPECT_21_9: aspect = 21.0 / 9.0; break;
        case DOOM_ASPECT_32_9: aspect = 32.0 / 9.0; break;
        case DOOM_ASPECT_FIT:
            if (drawable_width > 0 && drawable_height > 0)
                aspect = fmax(4.0 / 3.0,
                    fmin(32.0 / 9.0, (double)drawable_width / drawable_height));
            break;
        default: break;
        }
    }
    /* The 256x224 guest field displays as 4:3 (7:6 pixel aspect). Rounding
     * to an even width leaves its stock center on an exact integer offset. */
    int width = 2 * (int)floor((DOOM_STOCK_WIDTH * aspect / (4.0 / 3.0)) / 2 + 0.5);
    if (width < DOOM_STOCK_WIDTH) width = DOOM_STOCK_WIDTH;
    if (width > DOOM_MAX_WIDTH) width = DOOM_MAX_WIDTH;
    return (DoomViewport){width, (width - DOOM_STOCK_WIDTH) / 2, aspect};
}

DoomRect DoomDestination(DoomViewport viewport, int width, int height) {
    if (width <= 0 || height <= 0) return (DoomRect){0};
    double aspect = viewport.aspect;
    if (!isfinite(aspect) || aspect < 4.0 / 3.0 || aspect > 32.0 / 9.0)
        aspect = 4.0 / 3.0;
    int w, h;
    if ((double)width / height > aspect) {
        h = height;
        w = (int)floor(height * aspect + 0.5);
    } else {
        w = width;
        h = (int)floor(width / aspect + 0.5);
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    return (DoomRect){(width - w) / 2, (height - h) / 2, w, h};
}

int DoomWindowBaseWidth(int frame_width) {
    if (frame_width < DOOM_STOCK_WIDTH || frame_width > DOOM_MAX_WIDTH)
        frame_width = DOOM_STOCK_WIDTH;
    return (frame_width * 5 + 2) / 4;
}

double DoomPresentationHz(const DoomVideoSettings *settings,
                          double display_refresh) {
    if (!settings || !settings->fps_enabled) return 0;
    if (settings->fps && DoomValidFps(settings->fps)) return settings->fps;
    if (!isfinite(display_refresh) || display_refresh < 1) return 60;
    return fmin(display_refresh, 360);
}

double DoomPresentationAlpha(const DoomVideoSettings *settings, double alpha) {
    if (!settings || !settings->fps_enabled || !settings->interpolate ||
        !isfinite(alpha)) return 1;
    return fmax(0, fmin(1, alpha));
}

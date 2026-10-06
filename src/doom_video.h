#pragma once

#include <stdbool.h>

/* Presentation geometry only. The guest continues to render its 256x224
 * field and advance at the original simulation rate. */
enum { DOOM_STOCK_WIDTH = 256, DOOM_HEIGHT = 224, DOOM_MAX_WIDTH = 684 };

typedef enum DoomAspect {
    DOOM_ASPECT_STOCK,
    DOOM_ASPECT_16_9,
    DOOM_ASPECT_21_9,
    DOOM_ASPECT_32_9,
    DOOM_ASPECT_FIT,
    DOOM_ASPECT_COUNT
} DoomAspect;

typedef struct DoomVideoSettings {
    bool widescreen;
    DoomAspect aspect;
    bool fps_enabled;
    unsigned fps; /* 0 = Auto (display refresh). */
    bool interpolate;
} DoomVideoSettings;

typedef struct DoomViewport {
    int width;
    int extra; /* Columns on either side of the centered guest field. */
    double aspect;
} DoomViewport;

typedef struct DoomRect { int x, y, w, h; } DoomRect;

/* Default-disabled mods retain Fit/Auto as their remembered choices. */
void DoomVideoDefaults(DoomVideoSettings *settings);
void DoomVideoStock(DoomVideoSettings *settings);
const char *DoomAspectName(DoomAspect aspect);
bool DoomParseAspect(const char *text, DoomAspect *aspect);
bool DoomValidFps(unsigned fps);
bool DoomParseFps(const char *text, unsigned *fps);
/* FrameRate additionally accepts Original, which disables independent
 * presentation. Invalid input leaves both output values unchanged. */
bool DoomParseFrameRate(const char *text, unsigned *fps, bool *enabled);

/* Fit follows the drawable dimensions within 4:3..32:9. The even source
 * width retains the authentic center and SNES pixel aspect (7:6). */
DoomViewport DoomCalculateViewport(const DoomVideoSettings *settings,
                                   int drawable_width, int drawable_height);
DoomRect DoomDestination(DoomViewport viewport, int drawable_width,
                         int drawable_height);
/* Matches the shared host's standard 240-line window at scale 1. */
int DoomWindowBaseWidth(int frame_width);

/* 0 asks the shared host for original cadence. An enabled Auto setting
 * returns 60 when the display is unknown, including the host's pre-window
 * probe with refresh=0; this keeps decoupled presentation enabled. */
double DoomPresentationHz(const DoomVideoSettings *settings,
                          double display_refresh);
/* Composition may repeat the current capture when interpolation is disabled.
 * Geometry and guest simulation do not depend on this interpolation choice. */
double DoomPresentationAlpha(const DoomVideoSettings *settings, double alpha);

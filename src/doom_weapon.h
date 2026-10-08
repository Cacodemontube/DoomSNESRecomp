/* Presentation-only translation of the native weapon overlay. Artwork and
 * firing frames remain discrete; only matching artwork can move smoothly. */
#ifndef DOOM_WEAPON_H
#define DOOM_WEAPON_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

typedef struct DoomWeaponMotion {
    bool valid, presented;
    int x, y;
    double from_x, from_y, drawn_x, drawn_y;
    unsigned changed_field, interval;
    uint64_t artwork;
} DoomWeaponMotion;

static inline void DoomWeaponCapture(DoomWeaponMotion *m, int left, int top,
                                     uint64_t artwork, unsigned field,
                                     unsigned native_interval)
{
    const unsigned gap = field - m->changed_field;
    const bool continuous = m->valid && m->artwork == artwork &&
        abs(left - m->x) <= 32 && abs(top - m->y) <= 32;
    if (!continuous) {
        *m = (DoomWeaponMotion){.valid = true, .x = (int)left, .y = (int)top,
            .from_x = left, .from_y = top, .drawn_x = left, .drawn_y = top,
            .changed_field = field, .artwork = artwork};
    } else if (m->x != (int)left || m->y != (int)top) {
        m->from_x = m->presented ? m->drawn_x : m->x;
        m->from_y = m->presented ? m->drawn_y : m->y;
        m->x = (int)left; m->y = (int)top;
        /* Starting to walk after standing still must use a native render
         * interval, rather than spreading the first step over the idle time. */
        m->interval = native_interval && gap > native_interval * 2
            ? native_interval : (gap ? gap : 1);
        m->changed_field = field;
    }
}

static inline void DoomWeaponOffset(DoomWeaponMotion *m, unsigned field,
                                    float alpha, bool interpolate,
                                    int *dx, int *dy)
{
    double t = 1;
    if (interpolate && m->valid && m->interval) {
        double phase = field - m->changed_field;
        t = fmin(1, (phase + (isfinite(alpha) ? fmax(0, fmin(1, alpha)) : 0)) /
                       m->interval);
    }
    m->drawn_x = m->from_x + (m->x - m->from_x) * t;
    m->drawn_y = m->from_y + (m->y - m->from_y) * t;
    m->presented = m->valid;
    *dx = (int)lround(m->drawn_x - m->x);
    *dy = (int)lround(m->drawn_y - m->y);
}
#endif

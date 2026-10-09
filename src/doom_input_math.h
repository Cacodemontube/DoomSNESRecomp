#ifndef DOOM_INPUT_MATH_H
#define DOOM_INPUT_MATH_H
#include <math.h>
#include <stdint.h>
/* DOOM-FX rlplayer.a CJY5200/5300 converts X to 64 angle units/count.
 * Positive host X turns right (subtracts from the SNES angle). Accumulated
 * relative counts replace last-packet velocity times FPSRatio, consuming
 * each movement once regardless of the guest's variable rendering rate. */
static inline uint16_t DoomMouseAngle(uint16_t angle, double counts) {
    return (uint16_t)(angle - (int)lround(fmax(-256, fmin(256, counts)) * 64)) & 0xfffe;
}
static inline double DoomMousePitch(double pitch, double counts, int invert) {
    return fmax(-42, fmin(42, pitch - counts * (invert ? -0.25 : 0.25)));
}
enum { DOOM_KEY_FORWARD=1, DOOM_KEY_BACK=2, DOOM_KEY_LEFT=4,
       DOOM_KEY_RIGHT=8, DOOM_KEY_RUN=16, DOOM_KEY_USE=32,
       DOOM_KEY_PAUSE=64, DOOM_KEY_FIRE=128, DOOM_KEY_WEAPON=256,
       DOOM_KEY_MAP=512 };
static inline uint16_t DoomKeyboardPad(unsigned keys) {
    uint16_t pad = 0;
    if ((keys & 3) == DOOM_KEY_FORWARD) pad |= 1u<<4;
    if ((keys & 3) == DOOM_KEY_BACK) pad |= 1u<<5;
    if ((keys & 12) == DOOM_KEY_LEFT) pad |= 1u<<10;
    if ((keys & 12) == DOOM_KEY_RIGHT) pad |= 1u<<11;
    /* Retail USA useID8: B run, A use, Y fire, X weapon. */
    if (keys & DOOM_KEY_RUN) pad |= 1u<<0;
    if (keys & DOOM_KEY_USE) pad |= 1u<<8;
    if (keys & DOOM_KEY_PAUSE) pad |= 1u<<3;
    if (keys & DOOM_KEY_FIRE) pad |= 1u<<1;
    if (keys & DOOM_KEY_WEAPON) pad |= 1u<<9;
    if (keys & DOOM_KEY_MAP) pad |= 1u<<2;
    return pad;
}
#endif

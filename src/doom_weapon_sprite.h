#ifndef DOOM_WEAPON_SPRITE_H
#define DOOM_WEAPON_SPRITE_H
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

typedef struct DoomWeaponTile {
    int x, y;
    uint8_t pixels[64];
    uint8_t palette;
} DoomWeaponTile;

static inline uint16_t DoomWeaponWord(const uint8_t *ram, unsigned at) {
    return ram[at] | ((uint16_t)ram[at + 1] << 8);
}
static inline int DoomWeaponOamX(const uint16_t *oam, const uint8_t *high,
                                 unsigned slot) {
    int x = (oam[slot * 2] & 255) |
            (((high[slot >> 2] >> ((slot & 3) * 2)) & 1) << 8);
    return x >= 256 ? x - 512 : x;
}
/* Match the game's immutable six-byte precomputed OAM entries to actual
 * scanout OAM. Hidden Y=0 entries retain their original uncut Y in this table.
 * Priority is excluded because partial invisibility changes it at runtime. */
static inline bool DoomWeaponLayoutMatches(const uint8_t *ram, unsigned size,
    unsigned at, const uint16_t *oam, const uint8_t *high, unsigned count,
    int bottom)
{
    if (!ram || !count || count > 96 || at > size ||
        count * 6 > size - at) return false;
    int first_x = (int16_t)DoomWeaponWord(ram, at);
    int first_y = (int16_t)DoomWeaponWord(ram, at + 2);
    int anchor_x = DoomWeaponOamX(oam, high, 32);
    int anchor_y = oam[64] >> 8;
    for (unsigned i = 0; i < count; i++) {
        unsigned slot = 32 + i, address = at + i * 6;
        int x = anchor_x + (int16_t)DoomWeaponWord(ram, address) - first_x;
        int y = anchor_y + (int16_t)DoomWeaponWord(ram, address + 2) - first_y;
        if (x < -256 || x > 255 || y < -256 || y > 511 ||
            x != DoomWeaponOamX(oam, high, slot) ||
            (DoomWeaponWord(ram, address + 4) & 0xcfff) !=
                (oam[slot * 2 + 1] & 0xcfff)) return false;
        unsigned actual_y = oam[slot * 2] >> 8;
        if (actual_y ? y != (int)actual_y : y < bottom) return false;
    }
    return true;
}
static inline unsigned DoomWeaponFindLayout(const uint8_t *ram, unsigned size,
    unsigned cached, const uint16_t *oam, const uint8_t *high, unsigned count,
    int bottom)
{
    if (DoomWeaponLayoutMatches(ram, size, cached, oam, high, count, bottom))
        return cached;
    if (!ram || !count || count > 96 || size < count * 6) return UINT_MAX;
    for (unsigned at = 0; at <= size - count * 6; at += 2) {
        if ((DoomWeaponWord(ram, at + 4) & 0xcfff) != (oam[65] & 0xcfff))
            continue;
        if (DoomWeaponLayoutMatches(ram, size, at, oam, high, count, bottom))
            return at;
    }
    return UINT_MAX;
}
static inline void DoomWeaponDecodeTile(uint8_t *pixels,
                                        const uint16_t *vram,
                                        unsigned base, unsigned attr)
{
    for (unsigned y = 0; y < 8; y++) {
        unsigned row = (attr & 0x8000) ? 7 - y : y;
        unsigned at = (base + (attr & 255) * 16 + row) & 0x7fff;
        uint32_t planes = vram[at] | ((uint32_t)vram[(at + 8) & 0x7fff] << 16);
        for (unsigned x = 0; x < 8; x++) {
            unsigned bit = (attr & 0x4000) ? x : 7 - x;
            pixels[y * 8 + x] = ((planes >> bit) & 1) |
                (((planes >> (bit + 8)) & 1) << 1) |
                (((planes >> (bit + 16)) & 1) << 2) |
                (((planes >> (bit + 24)) & 1) << 3);
        }
    }
}
static inline uint32_t DoomWeaponComposite(uint32_t weapon,
                                           uint32_t background,
                                           bool translucent)
{
    if (!translucent) return weapon;
    return 0xff000000u | (((weapon & background) & 0xffffffu) +
                           (((weapon ^ background) & 0xfefefeu) >> 1));
}
#endif
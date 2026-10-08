#include "doom_weapon.h"
#include "doom_weapon_sprite.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #test); exit(1); } } while (0)
static void Capture(DoomWeaponMotion *m, int x, int y, unsigned field) {
    DoomWeaponCapture(m, x, y, 123, field, 6);
}
int main(void) {
    DoomWeaponMotion m = {0};
    int dx, dy;
    Capture(&m, 20, 20, 10);
    DoomWeaponOffset(&m, 10, 0, true, &dx, &dy);
    CHECK(dx == 0 && dy == 0);
    Capture(&m, 26, 26, 16);
    DoomWeaponOffset(&m, 16, 0, true, &dx, &dy);
    CHECK(dx == -6 && dy == -6);
    DoomWeaponOffset(&m, 18, 1, true, &dx, &dy);
    CHECK(dx == -3 && dy == -3);
    /* A shorter next update rebases at the pose actually displayed. */
    Capture(&m, 22, 22, 19);
    DoomWeaponOffset(&m, 19, 0, true, &dx, &dy);
    CHECK(dx == 1 && dy == 1);
    DoomWeaponOffset(&m, 22, 0, true, &dx, &dy);
    CHECK(dx == 0 && dy == 0);
    /* Native endpoint when interpolation is disabled. */
    Capture(&m, 28, 28, 25);
    DoomWeaponOffset(&m, 25, 0, false, &dx, &dy);
    CHECK(dx == 0 && dy == 0);
    /* Artwork changes, disappearance and discontinuities snap immediately. */
    DoomWeaponCapture(&m, 28, 28, 456, 26, 6);
    DoomWeaponOffset(&m, 26, 0, true, &dx, &dy);
    CHECK(dx == 0 && dy == 0);
    m = (DoomWeaponMotion){0};
    CHECK(!m.valid && !m.presented);
    Capture(&m, 2, 2, 28);
    Capture(&m, 40, 40, 29);
    DoomWeaponOffset(&m, 29, NAN, true, &dx, &dy);
    CHECK(dx == 0 && dy == 0);
    Capture(&m, 46, 46, 1000);
    DoomWeaponOffset(&m, 1003, 0, true, &dx, &dy);
    CHECK(dx == -3 && dy == -3); /* Restart after a long idle. */
    static uint16_t vram[0x8000];
    uint8_t pixels[64];
    vram[0] = 0x8080; vram[8] = 0x8080; /* Colour 15 at top left. */
    DoomWeaponDecodeTile(pixels, vram, 0, 0);
    CHECK(pixels[0] == 15 && pixels[1] == 0 && pixels[8] == 0);
    DoomWeaponDecodeTile(pixels, vram, 0, 0xc000);
    CHECK(pixels[63] == 15 && pixels[0] == 0);
    CHECK(DoomWeaponComposite(0xff204080, 0xff80a0c0, false) == 0xff204080);
    CHECK(DoomWeaponComposite(0xff204080, 0xff80a0c0, true) == 0xff5070a0);
    /* Precomputed layout restores a tile which the game hid at the HUD. */
    uint8_t ram[24] = {0}, high[32] = {0};
    uint16_t oam[256] = {0};
    ram[4] = 80; ram[5] = 0x13;
    ram[8] = 8; ram[10] = 81; ram[11] = 0x13;
    oam[64] = (160 << 8) | 100; oam[65] = 0x1350;
    oam[66] = 100; oam[67] = 0x1351; /* Y=0; uncut Y is 168. */
    CHECK(DoomWeaponFindLayout(ram, sizeof(ram), UINT_MAX, oam, high, 2, 167) == 0);
    oam[65] &= 0xcfff; oam[67] &= 0xcfff; /* Invisibility priority. */
    CHECK(DoomWeaponLayoutMatches(ram, sizeof(ram), 0, oam, high, 2, 167));
    ram[10] = 82;
    CHECK(DoomWeaponFindLayout(ram, sizeof(ram), 0, oam, high, 2, 167) == UINT_MAX);
    CHECK(!DoomWeaponLayoutMatches(ram, sizeof(ram), UINT_MAX, oam, high, 2, 167));
    puts("Weapon interpolation, clipping and opacity tests passed");
    return 0;
}

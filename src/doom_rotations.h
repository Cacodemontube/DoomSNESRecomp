#ifndef DOOM_ROTATIONS_H
#define DOOM_ROTATIONS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomRotationPatch {
    unsigned width, height;
    int left, top;
    /* Column-major; zero is transparent, palette entries are stored +1. */
    const uint16_t *pixels;
    bool flip;
} DoomRotationPatch;
/* Retail USA object types, from DOOM-FX rlobjects.i. Fixed items and
 * scenery use short records: +23 is unrelated memory, not rloImage. */
static inline bool DoomRotationsObjectFrameAllowed(unsigned type,unsigned image) {
    switch(type) {
    case 6: return image>=26 && image<=32;   /* Zombieman */
    case 7: return image>=39 && image<=45;   /* shotgun guy */
    case 8: return image>=51 && image<=58;   /* imp */
    case 9: return image>=65 && image<=72;   /* pinky */
    case 10:return image>=77 && image<=82;   /* cacodemon */
    case 11:return image>=87 && image<=91;   /* lost soul */
    case 12:return image>=95 && image<=102;  /* Baron */
    case 13:return image>=108 && image<=114; /* Cyberdemon */
    case 14:return image>=120 && image<=125; /* Spiderdemon */
    case 66:return image==218 || image==219; /* Baron / BFG flight only */
    case 67:return image==223;              /* player / Cyberdemon rocket */
    default:return false;
    }
}
bool DoomRotationsEnabled(void);
void DoomRotationsNativeOverrides(const uint8_t *original,const uint8_t *modified);
bool DoomRotationsNativeOverride(unsigned image);
bool DoomRotationsImport(const char *wad, const char *directory, char *error, size_t cap);
void DoomRotationsClear(void);
const DoomRotationPatch *DoomRotationsPatch(unsigned image, unsigned rotation);
unsigned DoomRotationsDirection(int dx, int dy, unsigned facing);
void DoomRotationsPalette(const uint16_t *cgram);
unsigned DoomRotationsPixel(unsigned pixel);
#ifdef __cplusplus
}
#endif
#endif

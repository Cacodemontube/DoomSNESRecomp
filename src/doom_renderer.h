#ifndef DOOM_RENDERER_H
#define DOOM_RENDERER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct SuperFx SuperFx;
typedef struct Ppu Ppu;

typedef struct DoomRendererStats {
    uint64_t captures, camera_passes, cache_hits, failures;
    uint64_t replay_instructions;
    uint64_t weapon_updates, weapon_interpolated_presentations;
    int weapon_offset_x, weapon_offset_y;
    uint64_t weapon_layout_fallbacks;
    unsigned weapon_tiles;
    bool weapon_translucent;
    unsigned snapshot_interval;
    bool supported, has_snapshot;
} DoomRendererStats;

/* All snapshots, geometry passes, and interpolation belong to presentation.
 * The emulated Super FX registers and cartridge RAM are never edited. */
void DoomRendererConfigure(SuperFx *fx, bool widescreen, bool interpolation,
                           unsigned width);
void DoomRendererPreparePpu(Ppu *ppu);
/* Called by the beam driver after each authentic visible scanline. */
void DoomRendererObserveLine(const Ppu *ppu, unsigned line, void *context);
void DoomRendererEndSimFrame(unsigned number);
bool DoomRendererDraw(Ppu *ppu, uint8_t *dst, size_t pitch,
                      unsigned width, unsigned height, float alpha);
bool DoomRendererDrawWeapon(Ppu *ppu, uint8_t *dst, size_t pitch,
                            unsigned width, unsigned height, float alpha,
                            bool world_redrawn);
void DoomRendererReset(void);
void DoomRendererGetStats(DoomRendererStats *out);

#endif

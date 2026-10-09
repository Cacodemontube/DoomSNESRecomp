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
    unsigned weapon_tiles, resolution_scale, resolution_segments;
    unsigned recovered_edges;
    bool menu_active;
    unsigned automap_lines;
    uint64_t automap_draws, automap_interpolated_presentations;
    bool automap_active, automap_transparent;
    bool weapon_translucent;
    unsigned snapshot_interval;
    bool supported, has_snapshot;
} DoomRendererStats;

/* All snapshots, geometry passes, and interpolation belong to presentation.
 * The emulated Super FX registers and cartridge RAM are never edited. */
void DoomRendererConfigure(SuperFx *fx, bool widescreen, bool interpolation,
                           unsigned width);
void DoomRendererPreparePpu(Ppu *ppu);
void DoomRendererSetResolution(unsigned scale);
void DoomRendererSetLook(bool enabled, double horizon_offset);
void DoomRendererSetTransparentMap(bool enabled);
bool DoomRendererAutomapOverlay(void);
bool DoomRendererDrawAutomap(uint8_t *dst,size_t pitch,unsigned width,unsigned scale,float alpha,bool world);
bool DoomRendererDrawResolution(uint8_t *dst, size_t pitch, unsigned width, unsigned scale);
void DoomRendererDrawMessages(uint8_t *dst, size_t pitch, unsigned width, unsigned scale);
void DoomRendererDrawMenu(uint8_t *dst, size_t pitch, unsigned width, unsigned scale);
void DoomRendererRememberHud(const uint8_t *field);
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

#ifndef DOOM_RENDERER_H
#define DOOM_RENDERER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct SuperFx SuperFx;
typedef struct Ppu Ppu;
struct SaveLoadInfo;
void DoomRendererSavePauseState(struct SaveLoadInfo *sli);
bool DoomRendererLoadPauseState(struct SaveLoadInfo *sli);

typedef struct DoomRendererStats {
    uint64_t captures, camera_passes, cache_hits, failures;
    uint64_t replay_instructions;
    /* Diagnostic-build wall times. Camera jobs can overlap in widescreen. */
    uint64_t profile_jobs, profile_raster_calls;
    double profile_job_ms, profile_build_ms, profile_draw_ms;
    double profile_metadata_ms, profile_decode_ms, profile_raster_ms;
    uint64_t async_submitted, async_accepted, async_stale, async_reused, async_cancelled;
    uint64_t async_verified, async_mismatches;
    uint64_t async_fallbacks;
    uint64_t async_temporary_fallbacks, async_retries;
    uint64_t weapon_updates, weapon_interpolated_presentations;
    int weapon_offset_x, weapon_offset_y;
    uint64_t weapon_layout_fallbacks;
    unsigned weapon_tiles, resolution_scale, resolution_segments;
    unsigned recovered_edges;
    bool menu_active;
    unsigned automap_lines, automap_enemy_arrows, automap_projectile_arrows;
    uint64_t automap_draws, automap_interpolated_presentations;
    uint64_t automap_world_interpolated_presentations;
    bool automap_active, automap_transparent;
    bool weapon_translucent;
    unsigned snapshot_interval;
    bool supported, has_snapshot;
    double look_offset, requested_look_offset;
} DoomRendererStats;

/* All snapshots, geometry passes, and interpolation belong to presentation.
 * The emulated Super FX registers and cartridge RAM are never edited. */
void DoomRendererConfigure(SuperFx *fx, bool widescreen, bool interpolation,
                           unsigned width);
void DoomRendererPreparePpu(Ppu *ppu);
void DoomRendererSetResolution(unsigned scale);
void DoomRendererSetLook(bool enabled, double horizon_offset);
double DoomRendererLookOffset(void);
void DoomRendererSetTransparentMap(bool enabled);
bool DoomRendererAutomapOverlay(void);
bool DoomRendererTransitionPending(void);
bool DoomRendererDrawAutomap(uint8_t *dst,size_t pitch,unsigned width,unsigned scale,float alpha,bool world);
bool DoomRendererDrawResolution(uint8_t *dst, size_t pitch, unsigned width, unsigned scale);
void DoomRendererDrawMessages(uint8_t *dst, size_t pitch, unsigned width, unsigned scale);
void DoomRendererDrawMenu(uint8_t *dst, size_t pitch, unsigned width, unsigned scale);
void DoomRendererDrawMenuPointer(uint8_t *dst,size_t pitch,unsigned width,unsigned scale);
void DoomRendererDrawCrosshair(uint8_t *dst,size_t pitch,unsigned width,unsigned height);
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

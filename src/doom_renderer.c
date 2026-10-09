/* Doom (USA), Reality engine presentation renderer.
 *
 * The original game builds its world in three 72-pixel strips. Capture just
 * before its BSP traversal, then replay the complete geometry/trace/draw
 * chain against private cartridge RAM for each camera. The side cameras see
 * actual additional level geometry; ray reprojection combines them into one
 * rectilinear view. The native CPU/GSU and beam driver still run unchanged.
 *
 * Address/structure reference: Randy Linden's original DOOM-FX source,
 * https://github.com/RandalLinden/DOOM-FX/tree/master/source
 * (rlbsp.a, rlbuild.a, rldraw.a, rlram7.a, rle.i, mkrlpixscale.c).
 * Retail addresses are checked against the loaded ROM before installing a
 * hook; the released source's later build has different function addresses.
 */
#include "doom_renderer.h"
#include "doom_projection.h"
#include "doom_shading.h"
#include "doom_sky.h"
#include "doom_weapon.h"
#include "doom_weapon_sprite.h"
#include "doom_resolution.h"

#include "common_cpu_infra.h"
#include "snes/snes.h"
#include "snes/ppu.h"
#include "snes/superfx.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifndef DOOM_RENDERER_SYNCHRONOUS
#include "desktop/sdl_compat.h"
#if SNESRECOMP_SDL3
typedef SDL_Semaphore DoomSemaphore;
#define DoomWaitSemaphore SDL_WaitSemaphore
#define DoomSignalSemaphore SDL_SignalSemaphore
#else
typedef SDL_sem DoomSemaphore;
#define DoomWaitSemaphore SDL_SemWait
#define DoomSignalSemaphore SDL_SemPost
#endif
#endif

enum {
    kBsp = 0xb9fe,
    kBuildB = 0xc811, kBuildC = 0xc80a,
    kDrawA = 0xe4f4, kDrawB = 0xe4eb, kDrawC = 0xe4e2,
    kViewX = 0x22, kViewY = 0x24, kViewZ = 0x26, kViewAngle = 0x28,
    kMessageCount = 0x20,
    kAutoMap = 0x1e6,
    kLevelIdentity = 0x7c, /* EMBSP, far pointer to the level BSP. */
    kSectorCount = 0x9a,
    kSectorData = 0x3080, kSectorSize = 14, kMaxSectors = 205,
    kObjects = 0x5a8e, kObjectSize = 38, kMaxObjects = 100,
    kNativeViewX = 20, kNativeViewY = 23, /* PPU line 24 is output row 23. */
    kOverlayHeight = 240,
    kGsuGo = 1 << 5,
    kMaxHistoryAge = 120,
    kWallLightScale = 0xcd2c, kObjectLightScale = 0xd425,
    kFloorPlot = 0xe357, kObjectPlotUnique = 0xe478,
    kObjectPlotRepeat = 0xe4c3, kSkyPlot = 0xe3b5,
    kMinPixX = 0x0a, kLightAdjust = 0xce, kSlopeYInverse = 0x200,
    kScaleTableRom = 0x198655, kColorMapsRom = 0x1cde00,
};

typedef struct Camera {
    int16_t x, y, z;
    uint16_t angle;
} Camera;

typedef struct RenderJob {
    Camera camera;
    double fraction;
    int yaw;
    uint8_t *picture, *ram;
    uint16_t *floors;
    uint8_t *objects;
    uint8_t *floor_used;
    DoomResolutionScene *resolution_scene;
    unsigned third;
    uint64_t instructions;
    bool success;
    char diagnostic[512];
} RenderJob;

#ifndef DOOM_RENDERER_SYNCHRONOUS
typedef struct RenderWorker {
    SDL_Thread *thread;
    DoomSemaphore *request, *done;
    bool shutdown;
    RenderJob job;
} RenderWorker;
#endif

typedef struct RenderState {
    SuperFx *fx;
    const uint8_t *rom_identity, *ram_identity;
    SuperFx snapshot;
    uint8_t *ram, *previous_ram, *work_ram, *presented_ram;
    uint8_t *side_ram[2];
#ifndef DOOM_RENDERER_SYNCHRONOUS
    RenderWorker workers[2];
    bool workers_unavailable;
#endif
    unsigned resolution_scale;
    bool look_enabled;
    double horizon_offset;
    DoomResolutionScene resolution_scenes[3];
    uint8_t pictures[3][DOOM_VIEW_WIDTH * DOOM_VIEW_HEIGHT];
    uint8_t object_pixels[3][DOOM_VIEW_WIDTH * DOOM_VIEW_HEIGHT];
    /* Sector texture pointers identify solid floors/ceilings; the two sky
     * sentinels identify the original panorama chosen by the private GSU. */
    uint16_t floors[3][DOOM_VIEW_WIDTH * DOOM_VIEW_HEIGHT];
    uint8_t floor_used[3][kMaxSectors];
    uint8_t floor_colors[2][kMaxSectors][DOOM_VIEW_HEIGHT][2];
    uint32_t palettes[DOOM_VIEW_HEIGHT][256];
    bool visible_rows[DOOM_VIEW_HEIGHT];
    uint8_t map_picture[kPpuBufWidth], map_x[kPpuBufWidth];
    uint8_t map_y[DOOM_VIEW_HEIGHT][kPpuBufWidth];
    unsigned map_width;
    bool map_wide;
    uint32_t weapon[kPpuBufWidth * kOverlayHeight];
    DoomWeaponMotion weapon_motion;
    int weapon_anchor_x, weapon_anchor_y;
    uint64_t weapon_artwork;
    bool weapon_pose_seen, weapon_translucent;
    DoomWeaponTile weapon_tiles[96];
    unsigned weapon_tile_count, weapon_layout_address;
    uint32_t weapon_palette[DOOM_VIEW_HEIGHT][128];
    unsigned weapon_palette_mask;
    Ppu *overlay_ppu;
    unsigned configured_width;
    bool widescreen, interpolation, hook_installed, supported;
    bool has_snapshot, has_previous, has_presented, cached, scene_motion;
    unsigned field, captured_field, interval, generation;
    unsigned cached_generation;
    Camera previous, current, cached_camera;
    uint64_t cached_scene_key;
    DoomRendererStats stats;
} RenderState;

static RenderState s;
static void StopWorkers(void);

static uint16_t ReadWord(const uint8_t *ram, unsigned address)
{
    return ram[address] | ((uint16_t)ram[address + 1] << 8);
}

static void WriteWord(uint8_t *ram, unsigned address, uint16_t value)
{
    ram[address] = (uint8_t)value;
    ram[address + 1] = (uint8_t)(value >> 8);
}

static Camera ReadCamera(const uint8_t *ram)
{
    Camera c = {(int16_t)ReadWord(ram, kViewX),
                (int16_t)ReadWord(ram, kViewY),
                (int16_t)ReadWord(ram, kViewZ),
                ReadWord(ram, kViewAngle)};
    return c;
}

static bool SupportedRom(const SuperFx *fx)
{
    /* The normal desktop ROM identity check also verifies the whole digest.
     * These signatures prevent an unsupported revision/mod from entering a
     * private replay at an unrelated instruction. */
    static const uint8_t bsp[] = {
        0xfa, 0xc0, 0x9a, 0xf9, 0x80, 0x71, 0xf0, 0x44, 0xba, 0x39,
        0xd9, 0xd9, 0x3d, 0xa8, 0x40, 0xa0, 0x5d, 0x3f, 0xdf,
        0x3d, 0xa0, 0x14,
    };
    static const uint8_t builds[] = {
        0xa3, 0x48, 0xa4, 0x6c, 0x05, 0x0c, 0x01,
        0xa3, 0x24, 0xa4, 0x48, 0x05, 0x05, 0x01,
        0xa3, 0x00, 0xa4, 0x24,
    };
    static const uint8_t draws[] = {
        0xf1, 0x51, 0xe1, 0xf2, 0xc5, 0xe9, 0x05, 0x10, 0x01,
        0xf1, 0x57, 0xe1, 0xf2, 0xce, 0xe9, 0x05, 0x07, 0x01,
        0xf1, 0x5d, 0xe1, 0xf2, 0xd7, 0xe9, 0xa0, 0x01,
    };
    static const uint8_t wall_light[] = {0xe3, 0x0a, 0x10, 0xd9, 0x49, 0x9f};
    static const uint8_t object_light[] = {0xea, 0xea, 0xb9, 0x6a, 0x29, 0x09};
    static const uint8_t floor_plot[] = {0x4e, 0x4c, 0xb4, 0x4e, 0x3c, 0x4c};
    static const uint8_t object_plot[] = {0x4c, 0xec, 0x09};
    static const uint8_t sky_plot[] = {0x4c, 0x3c, 0x4c, 0xb8, 0x3f, 0xdf};
    return fx && fx->rom && fx->ram && fx->rom_size == 0x200000 &&
        fx->ram_size >= 0x10000 &&
        memcmp(fx->rom + kBsp - 0x8000, bsp, sizeof(bsp)) == 0 &&
        memcmp(fx->rom + kBuildC - 0x8000, builds, sizeof(builds)) == 0 &&
        memcmp(fx->rom + kDrawC - 0x8000, draws, sizeof(draws)) == 0 &&
        memcmp(fx->rom + kWallLightScale - 0x8000, wall_light,
               sizeof(wall_light)) == 0 &&
        memcmp(fx->rom + kObjectLightScale - 0x8000, object_light,
               sizeof(object_light)) == 0 &&
        memcmp(fx->rom + kFloorPlot - 0x8000, floor_plot,
               sizeof(floor_plot)) == 0 &&
        memcmp(fx->rom + kObjectPlotUnique - 0x8000, object_plot,
               sizeof(object_plot)) == 0 &&
        memcmp(fx->rom + kObjectPlotRepeat - 0x8000, object_plot,
               sizeof(object_plot)) == 0 &&
        memcmp(fx->rom + kSkyPlot - 0x8000, sky_plot, sizeof(sky_plot)) == 0;
}

static void Capture(SuperFx *fx, uint32_t pc, void *context)
{
    (void)pc;
    (void)context;
    if (!(s.widescreen || s.interpolation || s.resolution_scale > 1 || s.look_enabled) || !s.ram ||
        ReadWord(fx->ram, kAutoMap) != 0)
        return;

    const unsigned now = s.field + 1;
    const Camera camera = ReadCamera(fx->ram);
    const unsigned gap = now - s.captured_field;
    const int dx = (int)camera.x - s.current.x;
    const int dy = (int)camera.y - s.current.y;
    const int dz = (int)camera.z - s.current.z;
    const int da = (int16_t)(uint16_t)(camera.angle - s.current.angle);
    const bool continuity = s.has_snapshot && gap > 0 && gap <= 60 &&
        abs(dx) <= 256 && abs(dy) <= 256 && abs(dz) <= 128 &&
        abs(da) <= 0x4000 &&
        memcmp(s.ram + kLevelIdentity, fx->ram + kLevelIdentity, 4) == 0;
    if (!continuity) s.weapon_motion = (DoomWeaponMotion){0};

    if (continuity) {
        /* Native renders can take unequal numbers of fields. Begin the next
         * interpolation at the last actually presented pose, rather than
         * jumping to an endpoint that a short native interval never reached. */
        memcpy(s.previous_ram, s.interpolation && s.has_presented
            ? s.presented_ram : s.ram, fx->ram_size);
        s.previous = s.interpolation && s.has_presented
            ? s.cached_camera : s.current;
        s.interval = gap;
    }
    s.scene_motion = false;
    if (continuity) {
        unsigned count = ReadWord(fx->ram, kSectorCount);
        if (count <= kMaxSectors)
            for (unsigned i = 0; i < count; i++) {
                const unsigned a = kSectorData + i * kSectorSize + 2;
                if (memcmp(s.previous_ram + a, fx->ram + a, 4))
                    s.scene_motion = true;
            }
        for (unsigned i = 1; i < kMaxObjects; i++) {
            const unsigned a = kObjects + i * kObjectSize;
            if (ReadWord(fx->ram, a) != 0xffff &&
                ReadWord(s.previous_ram, a) != 0xffff &&
                (memcmp(s.previous_ram + a + 6, fx->ram + a + 6, 10) ||
                 memcmp(s.previous_ram + a + 20, fx->ram + a + 20, 2)))
                s.scene_motion = true;
        }
    }
    s.has_previous = continuity;
    s.current = camera;
    s.snapshot = *fx;
    memcpy(s.ram, fx->ram, fx->ram_size);
    s.snapshot.ram = s.ram;
    s.snapshot.presentation = NULL;
    s.snapshot.pc_hooks = NULL;
    s.snapshot.pc_hook_count = s.snapshot.pc_hook_cap = 0;
    s.snapshot.enhancement_mode = kSuperFxEnhancement_PresentationReplay;
    s.captured_field = now;
    s.has_snapshot = true;
    s.cached = false;
    s.generation++;
    s.stats.captures++;
}

void DoomRendererConfigure(SuperFx *fx, bool widescreen, bool interpolation,
                           unsigned width)
{
    if (fx != s.fx || (fx && (fx->rom != s.rom_identity ||
                             fx->ram != s.ram_identity))) {
        /* A previous cartridge can already have been destroyed. Never
         * dereference its pointer here; session reset handles history. */
        StopWorkers();
        free(s.side_ram[0]); free(s.side_ram[1]);
        s.side_ram[0] = s.side_ram[1] = NULL;
        free(s.ram);
        free(s.previous_ram);
        free(s.work_ram);
        free(s.presented_ram);
        s.ram = s.previous_ram = s.work_ram = s.presented_ram = NULL;
        s.fx = fx;
        s.rom_identity = fx ? fx->rom : NULL;
        s.ram_identity = fx ? fx->ram : NULL;
        s.supported = SupportedRom(fx);
        for (unsigned i = 0; i < 3; i++) memset(&s.resolution_scenes[i], 0, sizeof(s.resolution_scenes[i]));
        s.hook_installed = false;
        s.has_snapshot = s.has_previous = s.has_presented = s.cached = false;
        s.weapon_motion = (DoomWeaponMotion){0};
        s.weapon_tile_count = 0;
    }
    if (s.widescreen != widescreen || s.interpolation != interpolation ||
        s.configured_width != width) {
        s.cached = false;
        s.weapon_motion = (DoomWeaponMotion){0};
        s.weapon_tile_count = 0;
    }
    s.widescreen = widescreen;
    s.interpolation = interpolation;
    s.configured_width = width;

    if (!s.supported || !(widescreen || interpolation || s.resolution_scale > 1 || s.look_enabled)) {
        if (s.hook_installed) {
            superfx_set_pc_hook(fx, kBsp, NULL, NULL);
            s.hook_installed = false;
        }
        /* Enabling a feature again must capture a fresh gameplay generation. */
        s.has_snapshot = s.has_previous = s.has_presented = s.cached = false;
        return;
    }
    if (!s.ram) {
        s.ram = malloc(fx->ram_size);
        s.previous_ram = malloc(fx->ram_size);
        s.work_ram = malloc(fx->ram_size);
        s.presented_ram = malloc(fx->ram_size);
        if (!s.ram || !s.previous_ram || !s.work_ram || !s.presented_ram) {
            free(s.ram); free(s.previous_ram); free(s.work_ram);
            free(s.presented_ram);
            s.ram = s.previous_ram = s.work_ram = s.presented_ram = NULL;
            s.stats.failures++;
            return;
        }
    }
    if (!s.hook_installed)
        s.hook_installed = superfx_set_pc_hook(fx, kBsp, Capture, NULL);
}

static bool WeaponGameplay(const Ppu *ppu)
{
    return ppu && s.fx && s.supported &&
        (!g_snes || !g_snes->ram || (g_snes->ram[0x2c] & 0x40)) &&
        ReadWord(s.fx->ram, kAutoMap) == 0;
}

static bool Gameplay(const Ppu *ppu)
{
    return ppu && s.fx && s.has_snapshot && s.supported &&
        (s.widescreen || s.interpolation || s.resolution_scale > 1 || s.look_enabled) &&
        /* Capture runs inside the next native field, before PreparePpu and
         * EndSimFrame publish it. That pending field is fresh, not unsigned
         * history-age underflow. Other future or old snapshots stay invalid. */
        (s.captured_field == s.field + 1 ||
         s.field - s.captured_field <= kMaxHistoryAge) &&
        (!g_snes || !g_snes->ram || (g_snes->ram[0x2c] & 0x40)) &&
        ReadWord(s.fx->ram, kAutoMap) == 0;
}

void DoomRendererPreparePpu(Ppu *ppu)
{
    if (!ppu) return;
    memset(s.visible_rows, 0, sizeof(s.visible_rows));
    s.weapon_pose_seen = false;
    s.weapon_tile_count = 0;
    s.weapon_palette_mask = 0;
    if (s.overlay_ppu != ppu) s.overlay_ppu = ppu;
    PpuSetOverlayCapture(ppu, kPpuOverlaySource_Obj, 0, 0, 0, 0, 0);
    if (!WeaponGameplay(ppu)) {
        s.weapon_motion = (DoomWeaponMotion){0};
        s.weapon_tile_count = 0;
        return;
    }
    memset(s.weapon, 0, sizeof(s.weapon));
    PpuBindOverlaySurface(ppu, kPpuOverlaySource_Obj, (uint8_t *)s.weapon,
                          kPpuBufWidth * sizeof(uint32_t));
    PpuSetOverlayCapture(ppu, kPpuOverlaySource_Obj,
                         0, kNativeViewY, 256, DOOM_VIEW_HEIGHT, 0);
    PpuSetOverlayOamRange(ppu, 32, 96);
}

void DoomRendererEndSimFrame(unsigned number)
{
    (void)number;
    s.field++;
    const unsigned previous_weapon_field = s.weapon_motion.changed_field;
    if (WeaponGameplay(s.overlay_ppu) && s.weapon_pose_seen)
        DoomWeaponCapture(&s.weapon_motion, s.weapon_anchor_x,
                          s.weapon_anchor_y, s.weapon_artwork, s.field, s.interval);
    else {
        s.weapon_motion = (DoomWeaponMotion){0};
        s.weapon_tile_count = 0;
    }
    if (s.weapon_motion.valid &&
        s.weapon_motion.changed_field != previous_weapon_field)
        s.stats.weapon_updates++;
}

static void RestartPrivateTask(SuperFx *fx, unsigned address)
{
    fx->r[15].data = (uint16_t)address;
    fx->r[15].modified = false;
    fx->pipeline = 1; /* STOP leaves a pipeline NOP before the next task. */
    fx->pipeline_pc = UINT32_MAX;
    fx->sfr |= kGsuGo;
    fx->enhancement_mode = kSuperFxEnhancement_PresentationReplay;
}

static void NormalizeWallLighting(SuperFx *fx, uint32_t pc, void *context)
{
    (void)pc;
    const RenderJob *job = context;
    const unsigned scale_address = superfx_reg(fx, 9);
    if (scale_address + 7 >= fx->ram_size) return;
    /* plwScale/ScaleF are consumed only by lighting at this point. Geometry
     * has already been traced; the texture stage replaces these with bank
     * and texture-address fields before drawing (rltracew4.a/rltracew5.a). */
    const double source_x = fx->ram[scale_address + 6] * 2;
    const unsigned scale = DoomNormalizeLightingScale(superfx_reg(fx, 3),
        ReadWord(fx->ram, scale_address + 1),
        DoomLightingScaleRatio(source_x, job->yaw * (DOOM_SIDE_YAW / 0x2000)));
    superfx_set_reg(fx, 3, scale >> 15);
    WriteWord(fx->ram, scale_address + 1, scale & 0x7fff);
}

static void NormalizeObjectLighting(SuperFx *fx, uint32_t pc, void *context)
{
    (void)pc;
    const RenderJob *job = context;
    const unsigned object = (uint16_t)(superfx_reg(fx, 11) - 1);
    if (object + 8 >= fx->ram_size) return;
    const int depth = (int16_t)ReadWord(fx->ram, object);
    const int x = (int16_t)ReadWord(fx->ram, object + 6);
    const double yaw = job->yaw * (DOOM_SIDE_YAW / 0x2000);
    const double common_depth = depth * cos(yaw) - x * sin(yaw);
    if (depth <= 0 || common_depth <= 0) return;
    const unsigned scale = DoomNormalizeLightingScale(superfx_reg(fx, 3),
        superfx_reg(fx, 2), depth / common_depth);
    /* Keep the object group's geometry scale in RAM untouched. These two
     * registers survive only through its colour-map calculation. */
    superfx_set_reg(fx, 2, scale & 0x7fff);
    superfx_set_reg(fx, 3, scale >> 15);
}

static void RecordFloorPixel(SuperFx *fx, uint32_t pc, void *context)
{
    (void)pc;
    RenderJob *job = context;
    /* Sky uses a different plot loop. Invulnerability uses the special map
     * at FE00 and must keep its original remapped colours. */
    if (superfx_reg(fx, 9) == 0xfe00) return;
    const unsigned pointer = superfx_reg(fx, 11);
    if (pointer < 2 || pointer > fx->ram_size) return;
    const uint16_t texture = ReadWord(fx->ram, pointer - 2);
    if (texture < kSectorData || texture >= kSectorData + kMaxSectors * kSectorSize)
        return;
    const unsigned component = (texture - kSectorData) % kSectorSize;
    if (component != 8 && component != 9) return;
    const unsigned x = superfx_reg(fx, 1), y = superfx_reg(fx, 2);
    if (x + 1 >= 72 || y >= DOOM_VIEW_HEIGHT || job->third >= 3) return;
    const unsigned offset = y * DOOM_VIEW_WIDTH + job->third * 72 + x;
    job->floors[offset] = job->floors[offset + 1] = texture;
    job->floor_used[(texture - kSectorData) / kSectorSize] |= 1u << (component - 8);
}

static void MaskObjectPixel(SuperFx *fx, uint32_t pc, void *context)
{
    (void)pc;
    RenderJob *job = context;
    if (!fx->colr && !(fx->por & 1)) return;
    const unsigned x = superfx_reg(fx, 1), y = superfx_reg(fx, 2);
    if (x >= 72 || y >= DOOM_VIEW_HEIGHT || job->third >= 3) return;
    const unsigned offset = y * DOOM_VIEW_WIDTH + job->third * 72 + x;
    job->floors[offset] = 0;
    if (job->objects) job->objects[offset] = 1;
    /* The second PLOT is the conditional branch's delay-slot instruction,
     * so it executes even for the final pixel in the object strip. */
    if (x + 1 < 72) {
        job->floors[offset + 1] = 0;
        if (job->objects) job->objects[offset + 1] = 1;
    }
}

static void RecordSkyPixel(SuperFx *fx, uint32_t pc, void *context)
{
    (void)pc;
    RenderJob *job = context;
    const unsigned x = superfx_reg(fx, 1), y = superfx_reg(fx, 2);
    if (x + 1 >= 72 || y >= DOOM_VIEW_HEIGHT || job->third >= 3) return;
    /* R5 is the sky address OR-mask computed by the original episode logic.
     * Preserve that decision rather than imposing a host episode mapping. */
    const unsigned sky = superfx_reg(fx, 5) & 0x4000
        ? kDoomSky2Surface : kDoomSky1Surface;
    if (job->resolution_scene) job->resolution_scene->sky2 = sky == kDoomSky2Surface;
    const unsigned offset = y * DOOM_VIEW_WIDTH + job->third * 72 + x;
    job->floors[offset] = job->floors[offset + 1] = (uint16_t)sky;
}

static bool RunPrivateTask(RenderJob *job, SuperFx *source, SuperFx *result,
                           unsigned stop_pc)
{
    source->enhancement_mode = kSuperFxEnhancement_PresentationReplay;
    const uint64_t before = source->instruction_count;
    *result = *source; /* Invalid-input rejection can leave result untouched. */
    bool completed;
    if (job->yaw || job->floors) {
        const bool draw = source->r[15].data == kDrawA ||
            source->r[15].data == kDrawB || source->r[15].data == kDrawC;
        const SuperFxReplayPcHook build_hooks[] = {
            {kWallLightScale, NormalizeWallLighting, job},
            {kObjectLightScale, NormalizeObjectLighting, job},
        };
        const SuperFxReplayPcHook draw_hooks[] = {
            {kFloorPlot, RecordFloorPixel, job},
            {kObjectPlotUnique, MaskObjectPixel, job},
            {kObjectPlotRepeat, MaskObjectPixel, job},
            {kSkyPlot, RecordSkyPixel, job},
        };
        completed = superfx_replay_snapshot_with_history(source, job->ram, result,
            draw ? draw_hooks : job->yaw ? build_hooks : NULL,
            draw ? 4 : job->yaw ? 2 : 0, false);
    } else {
        completed = superfx_replay_snapshot_with_history(source, job->ram, result,
                                                         NULL, 0, false);
    }
    /* A missing engine callback can jump to the ROM's zero-page STOP. GO
     * clearing alone does not prove that the geometry or picture finished. */
    const bool ok = completed && result->pbr == 0 &&
        result->r[15].data == stop_pc;
    const uint64_t executed = result->instruction_count >= before
        ? result->instruction_count - before : 0;
    job->instructions += executed;
    if (!ok) {
        const Camera camera = ReadCamera(job->ram);
        snprintf(job->diagnostic, sizeof(job->diagnostic),
            "[doom-replay-failed] field=%u captured=%u "
            "entry=%02x:%04x pipeline=%08x stop=%02x:%04x expected=%04x sfr=%04x "
            "instructions=%llu camera=%d,%d,%d,%04x sectors=%u "
            "pending=%04x/%02x/%u automap=%u\n",
            s.field, s.captured_field, source->pbr, source->r[15].data,
            source->pipeline_pc, result->pbr, result->r[15].data, stop_pc, result->sfr,
            (unsigned long long)executed, camera.x, camera.y, camera.z,
            camera.angle, ReadWord(job->ram, kSectorCount), source->ramar,
            source->ramdr, source->ramcl, ReadWord(job->ram, kAutoMap));
    }
    return ok;
}

static void DecodeThird(const SuperFx *fx, uint8_t *picture, unsigned third)
{
    /* 8bpp GSU pixels, SCMR height=160: tile columns are 20 tiles apart.
     * Each A/B/C draw reuses native columns 0..71 in this same buffer. */
    const unsigned base = (unsigned)fx->scbr << 10;
    for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++) {
        for (unsigned x = 0; x < DOOM_VIEW_WIDTH / 3; x++) {
            const unsigned tile = (x / 8) * 20 + y / 8;
            const unsigned address = base + tile * 64 + (y & 7) * 2;
            const unsigned shift = 7 - (x & 7);
            unsigned color = 0;
            for (unsigned plane = 0; plane < 8; plane++)
                color |= ((fx->ram[(address + (plane / 2) * 16 +
                                   (plane & 1)) & fx->ram_mask] >> shift) & 1)
                         << plane;
            picture[y * DOOM_VIEW_WIDTH + third * 72 + x] = (uint8_t)color;
        }
    }
}

static int16_t InterpolateCoordinate(int16_t previous, int16_t current,
                                     double fraction)
{
    return (int16_t)lround(previous + ((int)current - previous) * fraction);
}

static void InterpolateSectors(uint8_t *work_ram, double fraction)
{
    if (!s.has_previous || !s.interpolation || fraction >= 1) return;
    /* Heights may change independently of the camera (doors and lifts).
     * Use the same presentation time for both endpoints of every opening. */
    unsigned count = ReadWord(s.ram, kSectorCount);
    if (count > kMaxSectors) return;
    for (unsigned sector = 0; sector < count; sector++) {
        const unsigned address = kSectorData + sector * kSectorSize;
        for (unsigned component = 2; component <= 4; component += 2) {
            const int16_t previous = (int16_t)ReadWord(s.previous_ram,
                                                     address + component);
            const int16_t current = (int16_t)ReadWord(s.ram,
                                                    address + component);
            if (abs((int)current - previous) <= 128)
                WriteWord(work_ram, address + component,
                          (uint16_t)InterpolateCoordinate(previous, current,
                                                          fraction));
        }
    }
}

static void InterpolateObjects(uint8_t *work_ram, double fraction)
{
    if (!s.has_previous || !s.interpolation || fraction >= 1) return;
    /* rloX/Y are 16.16; the renderer reads their signed integer words.
     * Match slot identity and sector membership to avoid interpolating a
     * recycled enemy/projectile or carrying an object across a sector list. */
    /* Slot zero is the invisible single-player camera origin. */
    for (unsigned i = 1; i < kMaxObjects; i++) {
        const unsigned a = kObjects + i * kObjectSize;
        if (ReadWord(s.ram, a) == 0xffff ||
            ReadWord(s.previous_ram, a) == 0xffff ||
            s.ram[a + 4] != s.previous_ram[a + 4] ||
            s.ram[a + 5] != s.previous_ram[a + 5] ||
            s.ram[a + 22] != s.previous_ram[a + 22])
            continue;
        static const unsigned coordinates[] = {8, 12, 14};
        for (unsigned j = 0; j < 3; j++) {
            const unsigned p = a + coordinates[j];
            const int16_t old = (int16_t)ReadWord(s.previous_ram, p);
            const int16_t now = (int16_t)ReadWord(s.ram, p);
            if (abs((int)old - now) <= 256)
                WriteWord(work_ram, p,
                          (uint16_t)InterpolateCoordinate(old, now, fraction));
        }
        WriteWord(work_ram, a + 20,
                  DoomInterpolateAngle(ReadWord(s.previous_ram, a + 20),
                                       ReadWord(s.ram, a + 20), fraction));
    }
}

static uint64_t SceneKey(double fraction)
{
    /* Compare the actual integer coordinates consumed by the original GSU,
     * so a faster display can reuse a picture between subpixel changes. */
    uint64_t key = 1469598103934665603ull;
    if (!s.scene_motion || !s.has_previous || !s.interpolation) return key;
    unsigned count = ReadWord(s.ram, kSectorCount);
    if (count > kMaxSectors) count = 0;
    for (unsigned i = 0; i < count; i++) {
        for (unsigned component = 2; component <= 4; component += 2) {
            const unsigned a = kSectorData + i * kSectorSize + component;
            const int16_t before = (int16_t)ReadWord(s.previous_ram, a);
            const int16_t now = (int16_t)ReadWord(s.ram, a);
            const uint16_t value = abs((int)now - before) <= 128
                ? (uint16_t)InterpolateCoordinate(before, now, fraction)
                : (uint16_t)now;
            key = (key ^ value) * 1099511628211ull;
        }
    }
    for (unsigned i = 1; i < kMaxObjects; i++) {
        const unsigned a = kObjects + i * kObjectSize;
        if (ReadWord(s.ram, a) == 0xffff ||
            ReadWord(s.previous_ram, a) == 0xffff ||
            s.ram[a + 4] != s.previous_ram[a + 4] ||
            s.ram[a + 5] != s.previous_ram[a + 5] ||
            s.ram[a + 22] != s.previous_ram[a + 22])
            continue;
        static const unsigned components[] = {8, 12, 14};
        for (unsigned j = 0; j < 3; j++) {
            const unsigned p = a + components[j];
            const int16_t before = (int16_t)ReadWord(s.previous_ram, p);
            const int16_t now = (int16_t)ReadWord(s.ram, p);
            const uint16_t value = abs((int)now - before) <= 256
                ? (uint16_t)InterpolateCoordinate(before, now, fraction)
                : (uint16_t)now;
            key = (key ^ value) * 1099511628211ull;
        }
        key = (key ^ DoomInterpolateAngle(ReadWord(s.previous_ram, a + 20),
            ReadWord(s.ram, a + 20), fraction)) * 1099511628211ull;
    }
    return key;
}

static void SaveReplayFailure(Camera camera, double fraction, int yaw)
{
    static bool written;
    const char *path = getenv("DOOM_RENDER_FAILURE_DUMP");
    if (written || !path || !*path) return;
    FILE *file = fopen(path, "wb");
    if (!file) return;
    /* Local, same-build diagnostic fixture. Pointer values are replaced by
     * the replay probe; no authentic game memory is ever loaded from it. */
    const uint32_t size = sizeof(SuperFx);
    fwrite("DOOMRPL1", 1, 8, file);
    fwrite(&size, sizeof(size), 1, file);
    fwrite(&s.snapshot, sizeof(s.snapshot), 1, file);
    fwrite(s.ram, 1, s.snapshot.ram_size, file);
    fwrite(s.has_previous ? s.previous_ram : s.ram,
           1, s.snapshot.ram_size, file);
    fwrite(&camera, sizeof(camera), 1, file);
    fwrite(&fraction, sizeof(fraction), 1, file);
    fwrite(&yaw, sizeof(yaw), 1, file);
    fwrite(&s.has_previous, sizeof(s.has_previous), 1, file);
    fclose(file);
    written = true;
}

static void ExecuteRenderJob(RenderJob *job)
{
    const Camera camera = job->camera;
    const double fraction = job->fraction;
    const int yaw = job->yaw;
    if (s.resolution_scale > 1 || s.look_enabled || s.widescreen) {
        unsigned view = job->picture == s.pictures[0] ? 0 :
                        job->picture == s.pictures[1] ? 1 : 2;
        job->resolution_scene = &s.resolution_scenes[view];
        job->resolution_scene->count = 0;
        job->floors = s.floors[view];
        job->floor_used = s.floor_used[view];
        job->objects = s.object_pixels[view];
        memset(job->objects, 0, DOOM_VIEW_WIDTH * DOOM_VIEW_HEIGHT);
    }
    SuperFx source = s.snapshot, result;
    if (job->floors) {
        memset(job->floors, 0, DOOM_VIEW_WIDTH * DOOM_VIEW_HEIGHT * sizeof(uint16_t));
        memset(job->floor_used, 0, kMaxSectors);
    }
    memcpy(job->ram, s.ram, source.ram_size);
    InterpolateSectors(job->ram, fraction);
    InterpolateObjects(job->ram, fraction);
    WriteWord(job->ram, kViewX, (uint16_t)camera.x);
    WriteWord(job->ram, kViewY, (uint16_t)camera.y);
    WriteWord(job->ram, kViewZ, (uint16_t)camera.z);
    /* The single-player engine assumes its own player is at the view origin.
     * A camera between native endpoints can otherwise see the newer player
     * object and enter the unused multiplayer player-sprite callback (NULL
     * in the retail cartridge), stopping the build before floors are valid.
     * Keep that private player at its own presentation camera; world objects
     * and authentic simulation state remain independent. */
    WriteWord(job->ram, kObjects + 8, (uint16_t)camera.x);
    WriteWord(job->ram, kObjects + 12, (uint16_t)camera.y);
    /* Positive screen yaw turns right; guest angles increase to the left. */
    WriteWord(job->ram, kViewAngle, (uint16_t)(camera.angle - yaw));
    if (yaw)
        WriteWord(job->ram, kMessageCount, 0); /* Keep messages on main view. */
    if (!RunPrivateTask(job, &source, &result, 0xc835)) return;
    if (job->resolution_scene)
        DoomResolutionCapture(job->resolution_scene, job->ram, result.ram_size,
                              camera.z, result.rom, result.rom_size);
    if (job->resolution_scene) {
        DoomResolutionCaptureUv(job->resolution_scene, job->ram, result.ram_size, result.rom, result.rom_size);
        DoomResolutionCaptureSprites(job->resolution_scene, job->ram, result.ram_size, result.rom, result.rom_size);
    }

    static const unsigned tasks[] = {kDrawA, kBuildB, kDrawB,
                                     kBuildC, kDrawC};
    static const unsigned stops[] = {0xe528, 0xc835, 0xe549, 0xc835, 0xe570};
    for (unsigned i = 0; i < sizeof(tasks) / sizeof(tasks[0]); i++) {
        source = result;
        /* The replay API requires source RAM and destination RAM to be
         * distinct. Registers/cache stay in source; the RAM seed is the
         * immutable capture while the private working RAM keeps each job's
         * writes for the following job. */
        source.ram = s.ram;
        RestartPrivateTask(&source, tasks[i]);
        if (!RunPrivateTask(job, &source, &result, stops[i])) return;
        if (job->resolution_scene && (tasks[i] == kBuildB || tasks[i] == kBuildC)) {
            DoomResolutionCaptureUv(job->resolution_scene, job->ram, result.ram_size, result.rom, result.rom_size);
            DoomResolutionCaptureSprites(job->resolution_scene, job->ram, result.ram_size, result.rom, result.rom_size);
        }
        if (tasks[i] == kDrawA || tasks[i] == kDrawB || tasks[i] == kDrawC)
            DecodeThird(&result, job->picture, job->third++);
    }
    job->success = true;
}

static void MergeRenderJob(const RenderJob *job)
{
    /* Worker results, metrics and diagnostic files are published only after
     * all private cameras have joined. No worker writes renderer history. */
    s.stats.replay_instructions += job->instructions;
    if (job->success) s.stats.camera_passes++;
    else {
        if (getenv("DOOM_RENDER_STATS") && s.stats.failures < 32)
            fputs(job->diagnostic, stderr);
        SaveReplayFailure(job->camera, job->fraction, job->yaw);
    }
}

static bool RenderCameraInto(Camera camera, double fraction, int yaw,
                              uint8_t *picture, uint8_t *ram)
{
    RenderJob job = {0};
    job.camera = camera; job.fraction = fraction; job.yaw = yaw;
    job.picture = picture; job.ram = ram;
    if (yaw) {
        const unsigned view = picture == s.pictures[1] ? 1 : 2;
        job.floors = s.floors[view];
        job.floor_used = s.floor_used[view];
    }
    ExecuteRenderJob(&job);
    MergeRenderJob(&job);
    return job.success;
}

static bool RenderCamera(Camera camera, double fraction, int yaw,
                          uint8_t *picture)
{
    return RenderCameraInto(camera, fraction, yaw, picture, s.work_ram);
}

#ifndef DOOM_RENDERER_SYNCHRONOUS
static int RenderWorkerMain(void *context)
{
    RenderWorker *worker = context;
    for (;;) {
        DoomWaitSemaphore(worker->request);
        if (worker->shutdown) return 0;
        ExecuteRenderJob(&worker->job);
        DoomSignalSemaphore(worker->done);
    }
}

static void StopWorkers(void)
{
    for (unsigned i = 0; i < 2; i++)
        if (s.workers[i].thread) {
            s.workers[i].shutdown = true;
            DoomSignalSemaphore(s.workers[i].request);
        }
    for (unsigned i = 0; i < 2; i++) {
        RenderWorker *worker = &s.workers[i];
        if (worker->thread) SDL_WaitThread(worker->thread, NULL);
        if (worker->request) SDL_DestroySemaphore(worker->request);
        if (worker->done) SDL_DestroySemaphore(worker->done);
        memset(worker, 0, sizeof(*worker));
    }
    s.workers_unavailable = false;
}

static bool EnsureWorkers(void)
{
    if (s.workers_unavailable) return false;
    if (s.workers[0].thread && s.workers[1].thread) return true;
    for (unsigned i = 0; i < 2; i++) {
        RenderWorker *worker = &s.workers[i];
        worker->request = SDL_CreateSemaphore(0);
        worker->done = SDL_CreateSemaphore(0);
        if (worker->request && worker->done)
            worker->thread = SDL_CreateThread(RenderWorkerMain,
                i == 0 ? "doom-left-view" : "doom-right-view", worker);
        if (!worker->thread) {
            StopWorkers();
            s.workers_unavailable = true;
            return false;
        }
    }
    return true;
}
#else
static void StopWorkers(void) {}
#endif

static bool RenderViews(Camera camera, double fraction, bool wide)
{
    if (!wide) return RenderCamera(camera, fraction, 0, s.pictures[0]);
    for (unsigned i = 0; i < 2; i++) {
        if (!s.side_ram[i]) s.side_ram[i] = malloc(s.snapshot.ram_size);
        if (!s.side_ram[i]) return false;
    }
#ifndef DOOM_RENDERER_SYNCHRONOUS
    if (EnsureWorkers()) {
        /* Native execution and capture stay on the host thread. These jobs
         * share immutable ROM/endpoints and own disjoint GSU/RAM/pictures.
         * Every join completes before the host can run another native field. */
        for (unsigned i = 0; i < 2; i++) {
            RenderJob *job = &s.workers[i].job;
            memset(job, 0, sizeof(*job));
            job->camera = camera; job->fraction = fraction;
            job->yaw = i == 0 ? -0x2000 : 0x2000;
            job->picture = s.pictures[i + 1]; job->ram = s.side_ram[i];
            job->floors = s.floors[i + 1];
            job->floor_used = s.floor_used[i + 1];
            DoomSignalSemaphore(s.workers[i].request);
        }
        RenderJob center = {0};
        center.camera = camera; center.fraction = fraction;
        center.picture = s.pictures[0]; center.ram = s.work_ram;
        ExecuteRenderJob(&center);
        for (unsigned i = 0; i < 2; i++) DoomWaitSemaphore(s.workers[i].done);
        MergeRenderJob(&center);
        bool success = center.success;
        for (unsigned i = 0; i < 2; i++) {
            MergeRenderJob(&s.workers[i].job);
            success &= s.workers[i].job.success;
        }
        return success;
    }
#endif
    /* Platforms without worker resources retain the same isolated renderer. */
    return RenderCamera(camera, fraction, 0, s.pictures[0]) &&
        RenderCameraInto(camera, fraction, -0x2000, s.pictures[1], s.side_ram[0]) &&
        RenderCameraInto(camera, fraction, 0x2000, s.pictures[2], s.side_ram[1]);
}

static bool ColorWindowAt(const Ppu *ppu, unsigned x)
{
    const unsigned flags = GET_WINDOW_FLAGS(ppu, 5);
    bool a = (flags & kWindow1Enabled) &&
        x >= ppu->window1left && x <= ppu->window1right;
    bool b = (flags & kWindow2Enabled) &&
        x >= ppu->window2left && x <= ppu->window2right;
    if ((flags & (kWindow1Enabled | kWindow1Inversed)) ==
        (kWindow1Enabled | kWindow1Inversed)) a = !a;
    if ((flags & (kWindow2Enabled | kWindow2Inversed)) ==
        (kWindow2Enabled | kWindow2Inversed)) b = !b;
    if ((flags & (kWindow1Enabled | kWindow2Enabled)) ==
        (kWindow1Enabled | kWindow2Enabled)) {
        switch ((ppu->wbgobjlog >> 10) & 3) {
        case 0: return a || b;
        case 1: return a && b;
        case 2: return a != b;
        default: return a == b;
        }
    }
    return a || b;
}

static bool WindowCondition(unsigned mode, bool inside)
{
    return mode == 3 || (mode == 1 && !inside) || (mode == 2 && inside);
}

void DoomRendererObserveLine(const Ppu *ppu, unsigned line, void *context)
{
    (void)context;
    if (!WeaponGameplay(ppu) || line <= kNativeViewY ||
        line > kNativeViewY + DOOM_VIEW_HEIGHT) return;
    const unsigned row = line - 1 - kNativeViewY;
    s.visible_rows[row] = !PPU_forcedBlank(ppu) && PPU_mode(ppu) == 3 &&
        (ppu->screenEnabled[0] & 1);
    if (!s.visible_rows[row]) return;
    if (row == 0 && (ppu->oam[64] >> 8) != 0) {
        /* Slot 32 anchors the game's weapon tile group. Read its actual
         * scanout position instead of the visible bounding box: Doom hides
         * lower tiles at the HUD edge, which changes coverage while bobbing.
         * Track tile layout, not positions. New artwork is always drawn from
         * the current native overlay, without blending animation frames. */
        s.weapon_anchor_x = ppu->oam[64] & 255;
        if (ppu->highOam[8] & 1) s.weapon_anchor_x -= 256;
        s.weapon_anchor_y = ppu->oam[64] >> 8;
        /* Doom's invisibility path clears OBJ priority and toggles it back
         * near expiry. Follow the actual displayed mode, including blinking. */
        s.weapon_translucent = (ppu->oam[65] & 0x3000) == 0;
        uint64_t hash = UINT64_C(14695981039346656037);
        for (unsigned slot = 32; slot < 128; slot++) {
            const unsigned attr = ppu->oam[slot * 2 + 1];
            /* Empty slots use bank-zero tile 0, which aliases the changing
             * world framebuffer. They are not part of the weapon artwork. */
            if ((attr & 0x1f00) != (ppu->oam[65] & 0x1f00)) continue;
            hash = (hash ^ (attr & 0xcfff)) * UINT64_C(1099511628211);
            hash = (hash ^ ((ppu->highOam[slot >> 2] >>
                             ((slot & 3) * 2)) & 2)) * UINT64_C(1099511628211);
        }
        s.weapon_artwork = hash;
        s.weapon_pose_seen = true;
        unsigned count = 0;
        while (count < 96) {
            unsigned slot = 32 + count;
            if ((ppu->oam[slot * 2] >> 8) == 224 ||
                (ppu->oam[slot * 2 + 1] & 0x1ff) !=
                    ((ppu->oam[65] + count) & 0x1ff) ||
                ((ppu->highOam[slot >> 2] >> ((slot & 3) * 2)) & 2))
                break;
            count++;
        }
        const uint8_t *ram = g_snes ? g_snes->ram : NULL;
        unsigned layout = DoomWeaponFindLayout(ram, 0x20000,
            s.weapon_layout_address, ppu->oam, ppu->highOam, count,
            kNativeViewY + DOOM_VIEW_HEIGHT);
        if (layout != UINT_MAX) {
            s.weapon_layout_address = layout;
            const int first_x = (int16_t)ReadWord(ram, layout);
            const int first_y = (int16_t)ReadWord(ram, layout + 2);
            for (unsigned i = 0; i < count; i++) {
                unsigned address = layout + i * 6;
                unsigned attr = ppu->oam[(32 + i) * 2 + 1];
                DoomWeaponTile *tile = &s.weapon_tiles[i];
                tile->palette = (attr >> 9) & 7;
                s.weapon_palette_mask |= 1u << tile->palette;
                tile->x = s.weapon_anchor_x + (int16_t)ReadWord(ram, address) - first_x;
                tile->y = s.weapon_anchor_y + (int16_t)ReadWord(ram, address + 2) - first_y - 1;
                unsigned base = (attr & 0x100) ? PPU_objTileAdr2(ppu) : PPU_objTileAdr1(ppu);
                DoomWeaponDecodeTile(tile->pixels, PpuRenderVram(ppu), base, attr);
            }
            s.weapon_tile_count = count;
        } else if (count) {
            s.stats.weapon_layout_fallbacks++;
        }
    }
    for (unsigned palette = 0; palette < 8; palette++) {
        if (!(s.weapon_palette_mask & (1u << palette))) continue;
        for (unsigned i = 0; i < 16; i++) {
            unsigned color = ppu->cgram[128 + palette * 16 + i];
            s.weapon_palette[row][palette * 16 + i] = 0xff000000u |
                (uint32_t)ppu->brightnessMult[color & 31] << 16 |
                (uint32_t)ppu->brightnessMult[(color >> 5) & 31] << 8 |
                ppu->brightnessMult[(color >> 10) & 31];
        }
    }

    /* The guest ends each completed field in forced blank. Observe the
     * beam's display state instead of the post-VBlank register image, so
     * world colours follow the same brightness, palette and fades as PPU
     * scanout. Side-view backgrounds have the same colour-window treatment
     * as the centre of the native world viewport. The weapon remains an
     * independently resolved native OBJ overlay. */
    const bool inside = ColorWindowAt(ppu, 128);
    const bool clip = WindowCondition(PPU_clipMode(ppu), inside);
    const bool math = !WindowCondition(PPU_preventMathMode(ppu), inside) &&
        (PPU_mathEnabled(ppu) & 1);
    const bool half = math && PPU_halfColor(ppu) && !PPU_addSubscreen(ppu);
    for (unsigned i = 0; i < 256; i++) {
        const unsigned color = ppu->cgram[i];
        uint32_t pixel = 0xff000000u;
        for (unsigned component = 0; component < 3; component++) {
            int value = clip ? 0 : (color >> (component * 5)) & 31;
            if (math) {
                const int other = (ppu->fixedColor >> (component * 5)) & 31;
                value += PPU_subtractColor(ppu) ? -other : other;
                if (value < 0) value = 0;
                if (half) value /= 2;
                if (value > 31) value = 31;
            }
            pixel |= (uint32_t)ppu->brightnessMult[value] << (16 - component * 8);
        }
        s.palettes[row][i] = pixel;
    }
}

static void BuildProjectionMap(unsigned width, bool wide,
                                unsigned x0, unsigned x1)
{
    if (s.map_width == width && s.map_wide == wide) return;
    for (unsigned x = x0; x < x1; x++) {
        const int relative_x = (int)x - (int)width / 2;
        unsigned picture = 0;
        double sx = relative_x + DOOM_VIEW_WIDTH / 2.0;
        double scale_y = 1;
        if (relative_x < -(int)DOOM_VIEW_WIDTH / 2 ||
            relative_x >= (int)DOOM_VIEW_WIDTH / 2) {
            picture = relative_x < 0 ? 1 : 2;
            const double yaw = relative_x < 0 ? -DOOM_SIDE_YAW : DOOM_SIDE_YAW;
            double unused;
            DoomProjectRay(relative_x, 0, yaw, &sx, &unused);
            scale_y = 1 / (cos(yaw) + relative_x / DOOM_FOCAL * sin(yaw));
        }
        int source_x = (int)floor(sx);
        if (source_x < 0) source_x = 0;
        if (source_x >= (int)DOOM_VIEW_WIDTH) source_x = DOOM_VIEW_WIDTH - 1;
        s.map_picture[x] = (uint8_t)picture;
        s.map_x[x] = (uint8_t)source_x;
        for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++) {
            int source_y = (int)floor(DOOM_VIEW_HEIGHT / 2.0 +
                ((int)y - (int)DOOM_VIEW_HEIGHT / 2) * scale_y);
            if (source_y < 0) source_y = 0;
            if (source_y >= (int)DOOM_VIEW_HEIGHT) source_y = DOOM_VIEW_HEIGHT - 1;
            s.map_y[y][x] = (uint8_t)source_y;
        }
    }
    s.map_width = width;
    s.map_wide = wide;
}

static void BuildFloorShading(void)
{
    unsigned count = ReadWord(s.work_ram, kSectorCount);
    if (count > kMaxSectors) return;
    for (unsigned sector = 0; sector < count; sector++) {
        const unsigned used = s.floor_used[1][sector] | s.floor_used[2][sector];
        const unsigned address = kSectorData + sector * kSectorSize;
        for (unsigned ceiling = 0; ceiling < 2; ceiling++) {
            if (!(used & (1u << ceiling))) continue;
            const int height = (int16_t)ReadWord(s.work_ram,
                address + (ceiling ? 4 : 2)) - s.cached_camera.z;
            const unsigned raw_color = s.work_ram[address + 8 + ceiling];
            for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++) {
                /* Use the native inverse-slope/scale tables in the common
                 * camera. Reprojecting pre-shaded source rows instead moves
                 * the darkness bands and warps the original checker phase. */
                unsigned distance = (abs(height) * (unsigned)ReadWord(
                    s.work_ram, kSlopeYInverse + y * 2)) >> 9;
                if (distance > 7168) distance = 7168;
                const unsigned scale_address = kScaleTableRom + distance * 3;
                const unsigned scale = s.snapshot.rom[scale_address] |
                    ((unsigned)s.snapshot.rom[scale_address + 1] << 8) |
                    ((unsigned)s.snapshot.rom[scale_address + 2] << 16);
                const unsigned row = DoomFloorLightRow(s.work_ram[address + 1],
                    s.work_ram[kLightAdjust], scale);
                for (unsigned parity = 0; parity < 2; parity++)
                    s.floor_colors[ceiling][sector][y][parity] = s.snapshot.rom[
                        kColorMapsRom + (row + parity) * 256 + raw_color];
            }
        }
    }
}

bool DoomRendererDraw(Ppu *ppu, uint8_t *dst, size_t pitch,
                      unsigned width, unsigned height, float alpha)
{
    if (!Gameplay(ppu) || !dst || width < 256 || width > kPpuBufWidth ||
        pitch < width * sizeof(uint32_t) ||
        height < kNativeViewY + DOOM_VIEW_HEIGHT)
    {
        s.has_presented = false;
        return false;
    }
    bool visible = false;
    for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++)
        visible |= s.visible_rows[y];
    if (!visible) { s.has_presented = false; return false; }

    double fraction = 1;
    if (s.interpolation && s.has_previous && s.interval) {
        const double phase = s.field >= s.captured_field
            ? (double)(s.field - s.captured_field) : 0;
        fraction = (phase + (isfinite(alpha) ? fmax(0, fmin(1, alpha)) : 0)) /
                   s.interval;
        fraction = fmax(0, fmin(1, fraction));
    }
    Camera camera = s.current;
    if (fraction < 1) {
        camera.x = InterpolateCoordinate(s.previous.x, s.current.x, fraction);
        camera.y = InterpolateCoordinate(s.previous.y, s.current.y, fraction);
        camera.z = InterpolateCoordinate(s.previous.z, s.current.z, fraction);
        camera.angle = DoomInterpolateAngle(s.previous.angle, s.current.angle,
                                            fraction);
    }
    /* BSP sight rays and the rotational sine/cosine tables index angle>>6.
     * Replaying between those identical quantized poses only wastes work. */
    camera.angle &= 0xffc0;
    const uint64_t scene_key = SceneKey(fraction);
    const bool wide = s.widescreen && width > 256;
    if (!(s.cached && s.cached_generation == s.generation &&
          memcmp(&s.cached_camera, &camera, sizeof(camera)) == 0 &&
          s.cached_scene_key == scene_key)) {
        if (!RenderViews(camera, fraction, wide)) {
            s.stats.failures++;
            s.has_presented = s.cached = false;
            return false;
        }
        s.cached_camera = camera;
        s.cached_scene_key = scene_key;
        s.cached_generation = s.generation;
        s.cached = true;
        memcpy(s.presented_ram, s.work_ram, s.snapshot.ram_size);
        if (wide) BuildFloorShading();
    } else {
        s.stats.cache_hits++;
    }
    s.has_presented = true;

    const unsigned x0 = wide ? kNativeViewX : (width - 256) / 2 + kNativeViewX;
    const unsigned x1 = wide ? width - kNativeViewX : x0 + DOOM_VIEW_WIDTH;
    BuildProjectionMap(width, wide, x0, x1);
    for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++) {
        if (!s.visible_rows[y]) continue;
        uint32_t *line = (uint32_t *)(dst + (y + kNativeViewY) * pitch);
        for (unsigned x = x0; x < x1; x++) {
            const unsigned picture = s.map_picture[x];
            const unsigned offset = s.map_y[y][x] * DOOM_VIEW_WIDTH + s.map_x[x];
            unsigned color = s.pictures[picture][offset];
            const unsigned floor = picture ? s.floors[picture][offset] : 0;
            if (floor == kDoomSky1Surface || floor == kDoomSky2Surface) {
                /* The source cameras provide the correct world occlusion,
                 * but perspective warping their screen-space sky stretches
                 * mountain heights and shifts its phase at the joins. Use
                 * the common camera for the original panorama's artwork. */
                color = s.snapshot.rom[DoomSkyRomOffset(s.cached_camera.angle,
                    (int)x - (int)width / 2 + (int)DOOM_VIEW_WIDTH / 2,
                    y, floor == kDoomSky2Surface)];
            } else if (floor) {
                const unsigned sector = (floor - kSectorData) / kSectorSize;
                const unsigned ceiling = (floor - kSectorData) % kSectorSize - 8;
                const unsigned parity = DoomFloorDitherRow(0,
                    (int)x - (int)width / 2, y);
                color = s.floor_colors[ceiling][sector][y][parity];
            }
            line[x] = s.palettes[y][color];
        }
    }
    if (wide && s.resolution_scale <= 1)
        DoomRendererDrawResolution(dst, pitch, width, 1);
    if (s.resolution_scale <= 1 && !s.look_enabled)
        DoomRendererDrawWeapon(ppu, dst, pitch, width, height, alpha, true);
    return true;
}

bool DoomRendererDrawWeapon(Ppu *ppu, uint8_t *dst, size_t pitch,
                            unsigned width, unsigned height, float alpha,
                            bool world_redrawn)
{
    const unsigned scale = height / 224;
    const unsigned logical_width = scale ? width / scale : 0;
    if (!WeaponGameplay(ppu) || !dst || scale < 1 || scale > 4 ||
        height != scale * 224 || width != logical_width * scale ||
        logical_width < 256 || logical_width > kPpuBufWidth ||
        pitch < width * 4)
        return false;
    width = logical_width;
    int dx, dy;
    DoomWeaponOffset(&s.weapon_motion, s.field, alpha,
                     s.interpolation && world_redrawn, &dx, &dy);
    /* The stock picture already contains the native invisibility blend. */
    if (!world_redrawn && s.weapon_translucent) return true;
    s.stats.weapon_offset_x = dx;
    s.stats.weapon_offset_y = dy;
    if (dx || dy) s.stats.weapon_interpolated_presentations++;
    const unsigned center = (width - 256) / 2;
    if (s.weapon_tile_count) {
        /* Paint complete tiles first and clip at the final viewport boundary.
         * Native OAM has already hidden the bottom tiles; shifting its clipped
         * picture upward would expose holes instead of those missing pixels. */
        for (unsigned i = s.weapon_tile_count; i-- > 0;) {
            const DoomWeaponTile *tile = &s.weapon_tiles[i];
            for (int row = 0; row < 8; row++) {
                int y = tile->y + row + dy;
                if (y < kNativeViewY || y >= kNativeViewY + DOOM_VIEW_HEIGHT ||
                    !s.visible_rows[y - kNativeViewY]) continue;
                uint32_t *line = (uint32_t *)(dst + y * scale * pitch);
                for (int col = 0; col < 8; col++) {
                    int x = tile->x + col + dx;
                    unsigned color = tile->pixels[row * 8 + col];
                    if (!color || x < kNativeViewX ||
                        x >= kNativeViewX + DOOM_VIEW_WIDTH) continue;
                    for (unsigned sy = 0; sy < scale; sy++)
                        for (unsigned sx = 0; sx < scale; sx++) {
                            uint32_t *target = (uint32_t *)((uint8_t *)line + sy * pitch);
                            unsigned px = (center + x) * scale + sx;
                            target[px] = DoomWeaponComposite(
                                s.weapon_palette[y - kNativeViewY][tile->palette * 16 + color],
                                target[px], s.weapon_translucent);
                        }
                }
            }
        }
    } else {
        /* A frame whose precomputed layout is unavailable keeps native Y
         * clipping. Horizontal sway and opacity remain safe in this fallback. */
        for (unsigned y = kNativeViewY; y < kNativeViewY + DOOM_VIEW_HEIGHT; y++) {
            if (!s.visible_rows[y - kNativeViewY]) continue;
            uint32_t *line = (uint32_t *)(dst + y * scale * pitch);
            for (int x = kNativeViewX; x < kNativeViewX + DOOM_VIEW_WIDTH; x++) {
                int source_x = x - dx;
                if (source_x < 0 || source_x >= 256) continue;
                uint32_t weapon = s.weapon[y * kPpuBufWidth +
                                             kPpuExtraLeftRight + source_x];
                if (weapon >> 24)
                    for (unsigned sy = 0; sy < scale; sy++)
                        for (unsigned sx = 0; sx < scale; sx++) {
                            uint32_t *target = (uint32_t *)((uint8_t *)line + sy * pitch);
                            unsigned px = (center + x) * scale + sx;
                            target[px] = DoomWeaponComposite(weapon, target[px], s.weapon_translucent);
                        }
            }
        }
    }
    return true;
}
void DoomRendererReset(void)
{
    /* Cartridge replacement can reuse the same SuperFx allocation address.
     * A fresh configuration must validate the ROM and re-register its hook. */
    StopWorkers();
    s.fx = NULL;
    s.rom_identity = s.ram_identity = NULL;
    s.hook_installed = s.supported = false;
    s.has_snapshot = s.has_previous = s.has_presented = s.cached = false;
    s.field = s.captured_field = 0;
    s.weapon_layout_address = UINT_MAX;
    s.interval = 4;
    s.generation++;
    memset(s.weapon, 0, sizeof(s.weapon));
    s.weapon_motion = (DoomWeaponMotion){0};
    s.weapon_tile_count = 0;
    for (unsigned i = 0; i < 3; i++) memset(&s.resolution_scenes[i], 0, sizeof(s.resolution_scenes[i]));
    memset(s.visible_rows, 0, sizeof(s.visible_rows));
}

void DoomRendererGetStats(DoomRendererStats *out)
{
    if (!out) return;
    *out = s.stats;
    out->weapon_tiles = s.weapon_tile_count;
    out->resolution_scale = s.resolution_scale > 1 ? s.resolution_scale : 1;
    out->resolution_segments = s.resolution_scenes[0].count;
    out->weapon_translucent = s.weapon_translucent;
    out->snapshot_interval = s.interval;
    out->supported = s.supported;
    out->has_snapshot = s.has_snapshot;
}

void DoomRendererSetResolution(unsigned scale)
{
    if (scale < 1 || scale > 4) scale = 1;
    if (s.resolution_scale != scale) {
        s.resolution_scale = scale;
        s.cached = false;
        s.has_snapshot = s.has_previous = s.has_presented = false;
        for (unsigned i = 0; i < 3; i++) s.resolution_scenes[i].count = 0;
    }
}

void DoomRendererSetLook(bool enabled, double horizon_offset)
{
    if (s.look_enabled != enabled) {
        s.cached = false;
        s.has_snapshot = s.has_previous = s.has_presented = false;
    }
    s.look_enabled = enabled;
    s.horizon_offset = enabled ? fmax(-42, fmin(42, horizon_offset)) : 0;
}

bool DoomRendererDrawResolution(uint8_t *dst, size_t pitch,
                                unsigned width, unsigned scale)
{
    if (!dst || scale < 1 || scale > 4 || width < 256 ||
        width > 684 || pitch < width * scale * 4 || !s.cached ||
        !s.resolution_scenes[0].count) return false;
    bool wide = s.widescreen && width > 256;
    if (wide && (!s.resolution_scenes[1].count || !s.resolution_scenes[2].count))
        return false;
    unsigned x0 = wide ? 20 : (width - 256) / 2 + 20;
    unsigned x1 = wide ? width - 20 : x0 + 216;
    if (wide) {
        /* Sector and vertex identity identify the same native wall in all
         * visibility cameras. Prefer the central camera's texture anchor
         * and share it at both joins, including the current door position. */
        for (unsigned view=0;view<3;view++)
        for (unsigned i=0;i<s.resolution_scenes[view].count;i++) {
            DoomResolutionSegment *seg=&s.resolution_scenes[view].segments[i];
            for(unsigned component=0;component<2;component++) {
                seg->phase_valid[component]=false;
                for(unsigned ref=0;ref<3 && !seg->phase_valid[component];ref++)
                for(unsigned j=0;j<s.resolution_scenes[ref].count;j++) {
                    const DoomResolutionSegment *other=&s.resolution_scenes[ref].segments[j];
                    if(seg->near_sector!=other->near_sector || seg->vertex[0]!=other->vertex[0] ||
                       seg->vertex[1]!=other->vertex[1] || seg->texture[component]!=other->texture[component])continue;
                    if(DoomResolutionWallPhase(other,component,&seg->phase_u[component],&seg->phase_v[component])) {
                        seg->phase_valid[component]=true;break;
                    }
                }
            }
        }
    }
    /* Original low detail draws pairs of columns. Sample a common flat
     * camera at that same density across the complete widescreen view,
     * rather than magnifying the side cameras' framebuffer pixels. */
    unsigned columns = scale == 1 && wide ? 2 : 1;
    for (unsigned x = x0 * scale; x < x1 * scale; x += columns) {
        double rx = (x + columns * 0.5) / scale - width / 2.0;
        unsigned view = 0;
        double yaw = 0;
        if (rx < -108 || rx >= 108) {
            view = rx < 0 ? 1 : 2;
            yaw = rx < 0 ? -DOOM_SIDE_YAW : DOOM_SIDE_YAW;
        }
        double sx, sy;
        DoomProjectRay(rx, 0, yaw, &sx, &sy);
        double ratio = cos(yaw) + rx / DOOM_FOCAL * sin(yaw);
        DoomResolutionScene *scene = &s.resolution_scenes[view];
        if (!scene->count) return false;
        scene->horizon_offset = s.horizon_offset;
        scene->continuous_uv = wide;
        double depths[144 * 4];
        DoomResolutionRasterColumn(scene, s.snapshot.rom, s.snapshot.rom_size,
            s.cached_camera.angle, rx, (sx - 108) / DOOM_FOCAL, ratio,
            x, scale, dst, pitch, s.palettes, s.visible_rows, depths);
        if (scale > 1 || (wide && view != 0))
            DoomResolutionSprites(scene, s.snapshot.rom, s.snapshot.rom_size,
                (sx - 108) / DOOM_FOCAL, ratio, x, scale, dst, pitch,
                s.palettes, s.visible_rows, depths);
        if (columns == 2 && x + 1 < x1)
            for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++) {
                if (!s.visible_rows[y]) continue;
                uint32_t *line = (uint32_t *)(dst + (y + kNativeViewY) * pitch);
                line[x + 1] = line[x];
            }
    }
    if (scale == 1) {
        /* Low-resolution mouse look translates the native Super FX image.
         * Keep its object size, rounding, column duplication and transparent
         * texels instead of reconstructing billboards with continuous math.
         * The raster above supplies only the newly exposed edge rows. */
        BuildProjectionMap(width, wide, x0, x1);
        int shift = (int)lround(s.horizon_offset);
        for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++) {
            int source_y = (int)y - shift;
            if (!s.visible_rows[y] || source_y < 0 || source_y >= (int)DOOM_VIEW_HEIGHT)
                continue;
            uint32_t *line = (uint32_t *)(dst + (y + kNativeViewY) * pitch);
            for (unsigned x = x0; x < x1; x++) {
                unsigned picture = s.map_picture[x];
                unsigned offset = s.map_y[source_y][x] * DOOM_VIEW_WIDTH + s.map_x[x];
                /* In widescreen, retain only original central sprite pixels.
                 * World geometry and side sprites use the common projection. */
                if (wide && (picture != 0 || !s.object_pixels[0][offset])) continue;
                line[x] = s.palettes[y][s.pictures[picture][offset]];
            }
        }
    }
    return true;
}

/* Replay a captured failing presentation camera against the real GSU core.
 * Compile with superfx.c; arguments are a user-owned ROM and a same-build
 * DOOM_RENDER_FAILURE_DUMP fixture. Every pass must finish at its expected
 * build/draw STOP and preserve the captured core and RAM byte-for-byte. */
#ifndef DOOM_REPLAY_PARALLEL
#define DOOM_RENDERER_SYNCHRONOUS 1
#endif
#include "../src/doom_renderer.c"
#include "../src/doom_sky.h"

#include <stdio.h>
#include <time.h>

Snes *g_snes;
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Check failed: %s, line %u\n", #condition, __LINE__); \
        abort(); \
    } \
} while (0)

bool PpuBindOverlaySurface(Ppu *ppu, PpuOverlaySource source,
                           uint8_t *pixels, size_t pitch)
{
    (void)ppu; (void)source; (void)pixels; (void)pitch;
    return true;
}
bool PpuSetOverlayCapture(Ppu *ppu, PpuOverlaySource source,
                          int x, int y, int width, int height, uint8_t flags)
{
    (void)ppu; (void)source; (void)x; (void)y;
    (void)width; (void)height; (void)flags;
    return true;
}
bool PpuSetOverlayOamRange(Ppu *ppu, uint8_t first, uint8_t count)
{
    (void)ppu; (void)first; (void)count;
    return true;
}

typedef struct SkyLoopResult {
    unsigned calls, mask;
} SkyLoopResult;

static void FinishSkyRow(SuperFx *fx, uint32_t pc, void *context)
{
    (void)pc;
    SkyLoopResult *row = context;
    row->calls++;
    row->mask = superfx_reg(fx, 5);
    /* E3B8 follows LOOP and its second-PLOT delay slot. The original DrawA
     * epilogue flushes the pixel cache, then stops at E528. */
    superfx_hook_redirect(fx, 0xe520);
}

static void CheckNativeSkyLoop(void)
{
    /* This oracle executes the retail sky instructions, not a second copy of
     * the host address formula. Cover every row (including the Y>=128 column
     * borrow), both actual R5 texture masks, odd horizontal pan phases, and
     * pairs at negative/world coordinates beyond the native viewport. */
    static const uint16_t angles[] = {0, 0x0040, 0xffc0, 0x4000};
    static const int world_starts[] = {-72, 0, 72, 144, 216};
    uint8_t *ram = malloc(s.snapshot.ram_size);
    uint8_t *picture = malloc(sizeof(s.pictures[0]));
    CHECK(ram && picture);
    uint64_t pixels = 0;
    for (unsigned sky = 0; sky < 2; sky++) {
        for (unsigned a = 0; a < sizeof(angles) / sizeof(angles[0]); a++) {
            for (unsigned strip = 0; strip < sizeof(world_starts) / sizeof(world_starts[0]); strip++) {
                for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++) {
                    SuperFx source = s.snapshot, result;
                    source.pbr = source.rambr = 0;
                    source.scbr = 1;
                    source.scmr = 0x1f; /* native 8bpp, 160-row tile columns */
                    source.por = 1; /* draw index zero as an opaque sky pixel */
                    source.sfr = kGsuGo;
                    source.sreg = source.dreg = 0;
                    source.ramcl = source.romcl = 0;
                    memset(source.pixel, 0, sizeof(source.pixel));
                    memset(source.cache_valid, 0, sizeof(source.cache_valid));
                    superfx_set_reg(&source, 1, 0);
                    superfx_set_reg(&source, 2, (uint16_t)y);
                    superfx_set_reg(&source, 12, 36);
                    RestartPrivateTask(&source, 0xe375);
                    memcpy(ram, s.ram, source.ram_size);
                    WriteWord(ram, kMinPixX, (uint16_t)(world_starts[strip] / 2));
                    WriteWord(ram, kViewAngle, angles[a]);
                    /* Retail E39E/E39F shifts twice: EMNUM bit1 selects SKY2.
                     * Verify the resulting R5 rather than assuming selection. */
                    WriteWord(ram, 0x7a, sky ? 2 : 0);
                    SkyLoopResult row = {0};
                    const SuperFxReplayPcHook hook = {0xe3b8, FinishSkyRow, &row};
                    CHECK(superfx_replay_snapshot_with_hooks(&source, ram, &result, &hook, 1));
                    CHECK(result.pbr == 0 && result.r[15].data == 0xe528);
                    CHECK(row.calls == 1 && row.mask == (sky ? 0xc000u : 0x8000u));
                    DecodeThird(&result, picture, 0);
                    for (unsigned x = 0; x < 72; x++) {
                        const int world_x = world_starts[strip] + (int)x;
                        const unsigned offset = DoomSkyRomOffset(angles[a], world_x, y, sky);
                        const unsigned native = picture[y * DOOM_VIEW_WIDTH + x];
                        if (s.snapshot.rom[offset] != native) {
                            fprintf(stderr, "Sky mismatch sky=%u angle=%04x worldX=%d y=%u "
                                    "ROM=%05x expected=%u native=%u R5=%04x\n",
                                    sky + 1, angles[a], world_x, y, offset,
                                    s.snapshot.rom[offset], native, row.mask);
                            CHECK(s.snapshot.rom[offset] == native);
                        }
                        pixels++;
                    }
                }
            }
        }
    }
    printf("Native sky loop oracle: %llu pixels, 0 differences; SKY1/SKY2, "
           "angles0000/0040/FFC0/4000, worldX-72..287, all144 rows\n",
           (unsigned long long)pixels);
    free(picture);
    free(ram);
}

static void WriteSceneImage(const char *prefix, const char *suffix,
                           const uint32_t *pixels, unsigned width)
{
    char path[1024];
    CHECK(snprintf(path, sizeof(path), "%s%s", prefix, suffix) < (int)sizeof(path));
    FILE *file = fopen(path, "wb");
    CHECK(file);
    CHECK(fprintf(file, "P6\n%u 224\n255\n", width) > 0);
    for (unsigned p = 0; p < width * 224; p++) {
        const uint8_t rgb[] = {(uint8_t)(pixels[p] >> 16),
            (uint8_t)(pixels[p] >> 8), (uint8_t)pixels[p]};
        CHECK(fwrite(rgb, 1, 3, file) == 3);
    }
    CHECK(fclose(file) == 0);
}

static void ExportSkyScene(Camera camera, const char *cgram_path,
                           const char *prefix)
{
    const unsigned width = 682, center_x = width / 2 - DOOM_VIEW_WIDTH / 2;
    uint8_t cgram[512];
    FILE *file = fopen(cgram_path, "rb");
    CHECK(file && fread(cgram, 1, sizeof(cgram), file) == sizeof(cgram));
    CHECK(fgetc(file) == EOF && fclose(file) == 0);
    /* This diagnostic uses the captured, undamaged E1M1 CGRAM at native
     * brightness15 with no fixed-color adjustment. Every source/expected
     * pixel shares this palette; no ROM imagery is embedded in the test. */
    for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++) {
        s.visible_rows[y] = true;
        for (unsigned i = 0; i < 256; i++) {
            const unsigned color = ReadWord(cgram, i * 2);
            uint32_t rgb = 0xff000000u;
            for (unsigned c = 0; c < 3; c++) {
                const unsigned value = (color >> (c * 5)) & 31;
                rgb |= ((value << 3) | (value >> 2)) << (16 - c * 8);
            }
            s.palettes[y][i] = rgb;
        }
    }
    s.interpolation = false;
    RenderJob native = {0};
    native.camera = camera; native.fraction = 1;
    native.picture = s.pictures[0]; native.ram = s.work_ram;
    native.floors = s.floors[0]; native.floor_used = s.floor_used[0];
    ExecuteRenderJob(&native);
    CHECK(native.success);
    uint32_t *expected_center = calloc(width * 224, sizeof(uint32_t));
    uint32_t *present = calloc(width * 224, sizeof(uint32_t));
    uint32_t *oracle = calloc(width * 224, sizeof(uint32_t));
    uint32_t *mask = calloc(width * 224, sizeof(uint32_t));
    CHECK(expected_center && present && oracle && mask);
    for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++)
        for (unsigned x = 0; x < DOOM_VIEW_WIDTH; x++)
            expected_center[(y + kNativeViewY) * width + center_x + x] =
                s.palettes[y][s.pictures[0][y * DOOM_VIEW_WIDTH + x]];
    s.fx = &s.snapshot;
    s.has_snapshot = s.supported = s.widescreen = true;
    s.current = camera;
    s.presented_ram = malloc(s.snapshot.ram_size);
    CHECK(s.presented_ram);
    Ppu ppu = {0};
    CHECK(DoomRendererDraw(&ppu, (uint8_t *)present, width * 4, width, 224, 0));
    unsigned sky_pixels[2] = {0};
    for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++) {
        for (unsigned x = kNativeViewX; x < width - kNativeViewX; x++) {
            const unsigned picture = s.map_picture[x];
            const unsigned offset = s.map_y[y][x] * DOOM_VIEW_WIDTH + s.map_x[x];
            const unsigned surface = s.floors[picture][offset];
            if (surface != kDoomSky1Surface && surface != kDoomSky2Surface) continue;
            const unsigned position = (y + kNativeViewY) * width + x;
            const unsigned raw = s.snapshot.rom[DoomSkyRomOffset(camera.angle,
                (int)x - (int)center_x, y, surface == kDoomSky2Surface)];
            oracle[position] = s.palettes[y][raw];
            mask[position] = 0xffffffffu;
            sky_pixels[picture != 0]++;
        }
    }
    for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++) {
        const unsigned offset = (y + kNativeViewY) * width + center_x;
        CHECK(memcmp(expected_center + offset, present + offset,
                     DOOM_VIEW_WIDTH * sizeof(uint32_t)) == 0);
    }
    WriteSceneImage(prefix, ".ppm", present, width);
    WriteSceneImage(prefix, ".native.ppm", expected_center, width);
    WriteSceneImage(prefix, ".sky-oracle.ppm", oracle, width);
    WriteSceneImage(prefix, ".sky-mask.ppm", mask, width);
    printf("Private sky scene %d,%d,%d,%04x: native sky=%u side sky=%u pixels; "
           "native center unchanged; output=%s.ppm\n", camera.x, camera.y,
           camera.z, camera.angle, sky_pixels[0], sky_pixels[1], prefix);
    StopWorkers();
    free(s.presented_ram);
    s.presented_ram = NULL;
    free(expected_center); free(present); free(oracle); free(mask);
}

int main(int argc, char **argv)
{
    CHECK(argc == 3 || (argc == 10 && strcmp(argv[3], "--sky-scene") == 0));
    FILE *fixture = fopen(argv[2], "rb");
    CHECK(fixture);
    char magic[8];
    uint32_t size;
    CHECK(fread(magic, 1, 8, fixture) == 8);
    CHECK(memcmp(magic, "DOOMRPL1", 8) == 0);
    CHECK(fread(&size, sizeof(size), 1, fixture) == 1);
    CHECK(size == sizeof(SuperFx));
    CHECK(fread(&s.snapshot, sizeof(s.snapshot), 1, fixture) == 1);
    const unsigned ram_size = s.snapshot.ram_size;
    CHECK(ram_size >= 0x10000 && ram_size <= 0x20000);
    CHECK(s.snapshot.rom_size == 0x200000);
    s.ram = malloc(ram_size);
    s.previous_ram = malloc(ram_size);
    s.work_ram = malloc(ram_size);
    s.snapshot.rom = malloc(s.snapshot.rom_size);
    CHECK(s.ram && s.previous_ram && s.work_ram && s.snapshot.rom);
    CHECK(fread(s.ram, 1, ram_size, fixture) == ram_size);
    CHECK(fread(s.previous_ram, 1, ram_size, fixture) == ram_size);
    Camera camera;
    double fraction;
    int yaw;
    CHECK(fread(&camera, sizeof(camera), 1, fixture) == 1);
    CHECK(fread(&fraction, sizeof(fraction), 1, fixture) == 1);
    CHECK(fread(&yaw, sizeof(yaw), 1, fixture) == 1);
    CHECK(fread(&s.has_previous, sizeof(s.has_previous), 1, fixture) == 1);
    fclose(fixture);
    FILE *rom = fopen(argv[1], "rb");
    CHECK(rom);
    CHECK(fread(s.snapshot.rom, 1, s.snapshot.rom_size, rom) == s.snapshot.rom_size);
    fclose(rom);
    s.snapshot.ram = s.ram;
    s.snapshot.pc_hooks = NULL;
    s.snapshot.pc_hook_count = s.snapshot.pc_hook_cap = 0;
    s.snapshot.presentation = NULL;
    s.snapshot.ws_pixels = s.snapshot.ws_valid = NULL;
    s.snapshot.ws_present_pixels = s.snapshot.ws_present_valid = NULL;
    s.snapshot.ws_task_state = NULL;
    s.snapshot.ws_task_ram = NULL;
    s.interpolation = true;
    const SuperFx original = s.snapshot;
    uint8_t *original_ram = malloc(ram_size);
    uint8_t *original_previous = malloc(ram_size);
    CHECK(original_ram && original_previous);
    memcpy(original_ram, s.ram, ram_size);
    memcpy(original_previous, s.previous_ram, ram_size);
    if (argc == 10) {
        Camera scene = {(int16_t)strtol(argv[4], NULL, 0),
            (int16_t)strtol(argv[5], NULL, 0), (int16_t)strtol(argv[6], NULL, 0),
            (uint16_t)strtoul(argv[7], NULL, 0)};
        ExportSkyScene(scene, argv[8], argv[9]);
        CHECK(memcmp(&original, &s.snapshot, sizeof(original)) == 0);
        CHECK(memcmp(original_ram, s.ram, ram_size) == 0);
        CHECK(memcmp(original_previous, s.previous_ram, ram_size) == 0);
        return 0;
    }
    const clock_t start = clock();
    const int yaws[] = {0, -0x2000, 0x2000};
    if (yaw != 0) CHECK(RenderCamera(camera, fraction, yaw, s.pictures[0]));
    for (unsigned i = 0; i < 3; i++) {
        CHECK(RenderCamera(camera, fraction, yaws[i], s.pictures[i]));
        CHECK(memcmp(&original, &s.snapshot, sizeof(original)) == 0);
        CHECK(memcmp(original_ram, s.ram, ram_size) == 0);
        CHECK(memcmp(original_previous, s.previous_ram, ram_size) == 0);
        CHECK(ReadWord(s.work_ram, kObjects + 8) == (uint16_t)camera.x);
        CHECK(ReadWord(s.work_ram, kObjects + 12) == (uint16_t)camera.y);
    }
    printf("3 private camera passes completed; native state unchanged. "
        "camera=%d,%d,%d,%04x fraction=%.6f "
        "instructions=%llu milliseconds=%.3f\n", camera.x, camera.y,
        camera.z, camera.angle, fraction,
        (unsigned long long)s.stats.replay_instructions,
        (double)(clock() - start) * 1000 / CLOCKS_PER_SEC);

    /* Record native floor identity without changing its lighting. The host
     * recomputation must reproduce the completed native centre pixel exactly,
     * including the game's alternating adjacent colour maps. */
    uint8_t *native_picture = malloc(sizeof(s.pictures[0]));
    CHECK(native_picture);
    memcpy(native_picture, s.pictures[0], sizeof(s.pictures[0]));
    RenderJob native_floor = {0};
    native_floor.camera = camera; native_floor.fraction = fraction;
    native_floor.picture = s.pictures[0]; native_floor.ram = s.work_ram;
    native_floor.floors = s.floors[0]; native_floor.floor_used = s.floor_used[0];
    ExecuteRenderJob(&native_floor);
    CHECK(native_floor.success);
    CHECK(memcmp(native_picture, s.pictures[0], sizeof(s.pictures[0])) == 0);
    memcpy(s.floor_used[1], s.floor_used[0], sizeof(s.floor_used[0]));
    memset(s.floor_used[2], 0, sizeof(s.floor_used[2]));
    s.cached_camera = camera;
    BuildFloorShading();
    unsigned floor_count = 0, floor_differences = 0;
    unsigned sky_count[2] = {0}, sky_differences = 0;
    for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++) {
        for (unsigned x = 0; x < DOOM_VIEW_WIDTH; x++) {
            const unsigned offset = y * DOOM_VIEW_WIDTH + x;
            const unsigned floor = s.floors[0][offset];
            if (!floor) continue;
            if (floor == kDoomSky1Surface || floor == kDoomSky2Surface) {
                const unsigned sky = floor == kDoomSky2Surface;
                const unsigned expected = s.snapshot.rom[
                    DoomSkyRomOffset(camera.angle, (int)x, y, sky)];
                sky_count[sky]++;
                sky_differences += expected != s.pictures[0][offset];
                continue;
            }
            const unsigned sector = (floor - kSectorData) / kSectorSize;
            const unsigned ceiling = (floor - kSectorData) % kSectorSize - 8;
            const unsigned parity = DoomFloorDitherRow(0, (int)x, y);
            const unsigned expected = s.floor_colors[ceiling][sector][y][parity];
            floor_count++;
            if (expected != s.pictures[0][offset]) {
                if (floor_differences < 4)
                    fprintf(stderr, "Floor mismatch x=%u y=%u sector=%u ceiling=%u "
                            "expected=%u native=%u darkness=%u adjust=%u "
                            "colors=%u/%u\n", x, y, sector, ceiling,
                            expected, s.pictures[0][offset],
                            s.work_ram[kSectorData + sector * kSectorSize + 1],
                            s.work_ram[kLightAdjust],
                            s.floor_colors[ceiling][sector][y][0],
                            s.floor_colors[ceiling][sector][y][1]);
                floor_differences++;
            }
        }
    }
    printf("Native floor lighting oracle: %u pixels, %u differences\n",
           floor_count, floor_differences);
    CHECK(floor_count > 0 && floor_differences == 0);
    printf("Native scene sky oracle: SKY1=%u SKY2=%u pixels, %u differences\n",
           sky_count[0], sky_count[1], sky_differences);
    CHECK(sky_differences == 0);
    CheckNativeSkyLoop();
    CHECK(memcmp(&original, &s.snapshot, sizeof(original)) == 0);
    CHECK(memcmp(original_ram, s.ram, ram_size) == 0);
    CHECK(memcmp(original_previous, s.previous_ram, ram_size) == 0);
    free(native_picture);
#ifdef DOOM_REPLAY_PARALLEL
    uint8_t *reference = malloc(sizeof(s.pictures));
    uint16_t *reference_floors = malloc(sizeof(s.floors));
    CHECK(reference && reference_floors);
    memcpy(reference, s.pictures, sizeof(s.pictures));
    memcpy(reference_floors, s.floors, sizeof(s.floors));
    const uint64_t parallel_start = SDL_GetPerformanceCounter();
    CHECK(RenderViews(camera, fraction, true));
    CHECK(s.workers[0].thread && s.workers[1].thread);
    CHECK(s.side_ram[0] != s.side_ram[1] && s.side_ram[0] != s.work_ram &&
          s.side_ram[1] != s.work_ram);
    CHECK(memcmp(reference, s.pictures, sizeof(s.pictures)) == 0);
    CHECK(memcmp(reference_floors, s.floors, sizeof(s.floors)) == 0);
    CHECK(memcmp(&original, &s.snapshot, sizeof(original)) == 0);
    CHECK(memcmp(original_ram, s.ram, ram_size) == 0);
    CHECK(memcmp(original_previous, s.previous_ram, ram_size) == 0);
    printf("Parallel pictures match serial pictures byte-for-byte; "
           "milliseconds=%.3f\n",
           (double)(SDL_GetPerformanceCounter() - parallel_start) * 1000 /
               SDL_GetPerformanceFrequency());
    StopWorkers();
    StopWorkers();
    CHECK(!s.workers[0].thread && !s.workers[1].thread);
    CHECK(RenderViews(camera, fraction, true));
    CHECK(s.workers[0].thread && s.workers[1].thread);
    CHECK(memcmp(reference, s.pictures, sizeof(s.pictures)) == 0);
    CHECK(memcmp(reference_floors, s.floors, sizeof(s.floors)) == 0);
    StopWorkers();
    free(reference);
    free(reference_floors);
    free(s.side_ram[0]); free(s.side_ram[1]);
#endif
    free(original_ram); free(original_previous);
    free(s.ram); free(s.previous_ram); free(s.work_ram); free(s.snapshot.rom);
    return 0;
}

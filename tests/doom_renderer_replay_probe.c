/* Replay a captured failing presentation camera against the real GSU core.
 * Compile with superfx.c; arguments are a user-owned ROM and a same-build
 * DOOM_RENDER_FAILURE_DUMP fixture. Every pass must finish at its expected
 * build/draw STOP and preserve the captured core and RAM byte-for-byte. */
#ifndef DOOM_REPLAY_PARALLEL
#define DOOM_RENDERER_SYNCHRONOUS 1
#endif
#include "../src/doom_renderer.c"

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

int main(int argc, char **argv)
{
    CHECK(argc == 3);
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
    for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++) {
        for (unsigned x = 0; x < DOOM_VIEW_WIDTH; x++) {
            const unsigned offset = y * DOOM_VIEW_WIDTH + x;
            const unsigned floor = s.floors[0][offset];
            if (!floor) continue;
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

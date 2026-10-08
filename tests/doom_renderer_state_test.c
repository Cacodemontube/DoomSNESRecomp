/* Standalone host-state regressions; supply a user-owned Doom (USA) ROM.
 * The replay and overlay boundaries are stubbed, so this verifies lifecycle,
 * timing and raster policy without advancing any native game execution. */
#define DOOM_RENDERER_SYNCHRONOUS 1
#include "../src/doom_renderer.c"

#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Check failed: %s, line %u\n", #condition, __LINE__); \
        abort(); \
    } \
} while (0)

Snes *g_snes;
static unsigned registrations, replay_attempts, overlay_bindings;

bool superfx_set_pc_hook(SuperFx *fx, uint32_t pc, SuperFxPcHook *hook,
                        void *context)
{
    (void)context;
    CHECK(pc == kBsp);
    fx->pc_hook_count = hook != NULL;
    registrations += hook != NULL;
    return true;
}

bool superfx_replay_snapshot(const SuperFx *source, uint8_t *private_ram,
                             SuperFx *result)
{
    CHECK(source->ram != private_ram);
    *result = *source;
    result->ram = private_ram;
    replay_attempts++;
    return false; /* Exercise the bounded native fallback on guard failure. */
}

bool superfx_replay_snapshot_with_hooks(const SuperFx *source,
                                        uint8_t *private_ram, SuperFx *result,
                                        const SuperFxReplayPcHook *hooks,
                                        unsigned hook_count)
{
    (void)hooks; (void)hook_count;
    return superfx_replay_snapshot(source, private_ram, result);
}

uint16_t superfx_reg(const SuperFx *fx, unsigned n) { return fx->r[n].data; }
bool superfx_replay_snapshot_with_history(const SuperFx *source,
                                          uint8_t *private_ram, SuperFx *result,
                                          const SuperFxReplayPcHook *hooks,
                                          unsigned hook_count, bool history)
{
    (void)history;
    return superfx_replay_snapshot_with_hooks(source, private_ram, result, hooks, hook_count);
}
void superfx_set_reg(SuperFx *fx, unsigned n, uint16_t value)
{
    fx->r[n].data = value;
    fx->r[n].modified = true;
}

bool PpuBindOverlaySurface(Ppu *ppu, PpuOverlaySource source,
                           uint8_t *pixels, size_t pitch)
{
    (void)ppu;
    CHECK(source == kPpuOverlaySource_Obj && pixels != NULL);
    CHECK(pitch == kPpuBufWidth * sizeof(uint32_t));
    overlay_bindings++;
    return true;
}

bool PpuSetOverlayCapture(Ppu *ppu, PpuOverlaySource source,
                          int x, int y, int width, int height, uint8_t flags)
{
    (void)ppu; (void)x; (void)y; (void)width; (void)height; (void)flags;
    CHECK(source == kPpuOverlaySource_Obj);
    return true;
}

bool PpuSetOverlayOamRange(Ppu *ppu, uint8_t first, uint8_t count)
{
    (void)ppu;
    CHECK(first == 32 && count == 96);
    return true;
}

static void SetCamera(SuperFx *fx, int x, unsigned angle, int floor)
{
    WriteWord(fx->ram, kViewX, (uint16_t)x);
    WriteWord(fx->ram, kViewAngle, (uint16_t)angle);
    WriteWord(fx->ram, kSectorData + 2, (uint16_t)floor);
}

static void SetBrightness(Ppu *ppu, unsigned brightness)
{
    ppu->inidisp = (uint8_t)brightness;
    for (unsigned i = 0; i < 32; i++)
        ppu->brightnessMult[i] = ((i << 3) | (i >> 2)) * brightness / 15;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    SuperFx fx = {0};
    fx.rom_size = 0x200000;
    fx.rom = malloc(fx.rom_size);
    fx.ram_size = 0x10000; /* Retail cartridge RAM is 64 KiB. */
    fx.ram_mask = fx.ram_size - 1;
    fx.ram = calloc(1, fx.ram_size);
    FILE *rom = fopen(argv[1], "rb");
    CHECK(rom && fx.rom && fx.ram);
    CHECK(fread(fx.rom, 1, fx.rom_size, rom) == fx.rom_size);
    fclose(rom);
    CHECK(SupportedRom(&fx));
    WriteWord(fx.ram, kSectorCount, 1);
    for (unsigned i = 0; i < kMaxObjects; i++)
        WriteWord(fx.ram, kObjects + i * kObjectSize, 0xffff);

    DoomRendererReset();
    DoomRendererConfigure(&fx, true, true, 342);
    CHECK(s.supported && s.hook_installed && registrations == 1);
    SetCamera(&fx, 0, 0xff00, 0);
    Capture(&fx, kBsp, NULL);
    CHECK(s.has_snapshot && !s.has_previous);
    s.field = 4;
    SetCamera(&fx, 100, 0x100, 20);
    Capture(&fx, kBsp, NULL);
    CHECK(s.interval == 4 && s.previous.x == 0);

    /* A shorter subsequent native render must start at the last displayed
     * pose, including moving sector heights, rather than jump to x=100. */
    s.has_presented = true;
    s.cached_camera = (Camera){40, 0, 0, 0xff80};
    memcpy(s.presented_ram, s.ram, fx.ram_size);
    WriteWord(s.presented_ram, kSectorData + 2, 8);
    s.field = 7;
    SetCamera(&fx, 200, 0x300, 40);
    Capture(&fx, kBsp, NULL);
    CHECK(s.interval == 3 && s.previous.x == 40);
    CHECK(ReadWord(s.previous_ram, kSectorData + 2) == 8);
    memcpy(s.work_ram, s.ram, fx.ram_size);
    InterpolateSectors(s.work_ram, 0);
    CHECK(ReadWord(s.work_ram, kSectorData + 2) == 8);
    CHECK(InterpolateCoordinate(s.previous.x, s.current.x, 0) == 40);
    CHECK(DoomInterpolateAngle(s.previous.angle, s.current.angle, 0) == 0xff80);
    s.has_presented = true;
    s.cached_camera.x = 160;
    s.field = 12;
    SetCamera(&fx, 260, 0x500, 60);
    Capture(&fx, kBsp, NULL);
    CHECK(s.interval == 5 && s.previous.x == 160);

    /* Rotation in place still changes the intermediate sprite view, even
     * when the camera and all object coordinates remain stationary. */
    const unsigned object = kObjects + kObjectSize;
    WriteWord(s.ram, object, 0);
    WriteWord(fx.ram, object, 0);
    WriteWord(s.ram, object + 20, 0);
    WriteWord(fx.ram, object + 20, 0x2000);
    s.has_presented = false;
    s.field += 4;
    Capture(&fx, kBsp, NULL);
    CHECK(s.scene_motion && SceneKey(0) != SceneKey(0.5));

    /* The hidden self-player angle must not defeat quantized camera caching. */
    WriteWord(s.ram, object + 20, 0x2000);
    WriteWord(s.ram, kObjects, 0);
    WriteWord(fx.ram, kObjects, 0);
    WriteWord(s.ram, kObjects + 20, 0);
    WriteWord(fx.ram, kObjects + 20, 0x100);
    s.field += 4;
    Capture(&fx, kBsp, NULL);
    CHECK(!s.scene_motion && SceneKey(0) == SceneKey(0.5));

    /* Presentation floor identity must survive transparent sprite texels,
     * then be masked for both pixels of an opaque pair, including the final
     * pixel drawn in the conditional branch's delay slot. */
    SuperFx private_fx = fx;
    private_fx.ram = s.work_ram;
    RenderJob floor_job = {0};
    floor_job.third = 1;
    floor_job.floors = s.floors[1]; floor_job.floor_used = s.floor_used[1];
    private_fx.r[1].data = 70; private_fx.r[2].data = 100;
    private_fx.r[9].data = 0xde00; private_fx.r[11].data = 0x9006;
    WriteWord(private_fx.ram, 0x9004, kSectorData + 8);
    RecordFloorPixel(&private_fx, kFloorPlot, &floor_job);
    const unsigned floor_offset = 100 * DOOM_VIEW_WIDTH + 72 + 70;
    CHECK(s.floors[1][floor_offset] == kSectorData + 8);
    CHECK(s.floors[1][floor_offset + 1] == kSectorData + 8);
    CHECK(s.floor_used[1][0] == 1);
    private_fx.por = private_fx.colr = 0;
    MaskObjectPixel(&private_fx, kObjectPlotUnique, &floor_job);
    CHECK(s.floors[1][floor_offset] == kSectorData + 8);
    private_fx.colr = 110; private_fx.r[12].data = 1;
    MaskObjectPixel(&private_fx, kObjectPlotUnique, &floor_job);
    CHECK(!s.floors[1][floor_offset] && !s.floors[1][floor_offset + 1]);
    private_fx.r[9].data = 0xfe00;
    RecordFloorPixel(&private_fx, kFloorPlot, &floor_job);
    CHECK(!s.floors[1][floor_offset]); /* Invulnerable colour map stays native. */

    /* Sky uses the same occlusion metadata as floors, with its panorama
     * selected by the authentic private episode logic. Opaque object pairs
     * must mask sky coverage, while transparent object texels retain it. */
    private_fx.r[5].data = 0x8000;
    RecordSkyPixel(&private_fx, kSkyPlot, &floor_job);
    CHECK(s.floors[1][floor_offset] == kDoomSky1Surface &&
          s.floors[1][floor_offset + 1] == kDoomSky1Surface);
    private_fx.colr = 0;
    MaskObjectPixel(&private_fx, kObjectPlotRepeat, &floor_job);
    CHECK(s.floors[1][floor_offset + 1] == kDoomSky1Surface);
    private_fx.colr = 110;
    MaskObjectPixel(&private_fx, kObjectPlotRepeat, &floor_job);
    CHECK(!s.floors[1][floor_offset] && !s.floors[1][floor_offset + 1]);
    private_fx.r[5].data = 0xc000;
    RecordSkyPixel(&private_fx, kSkyPlot, &floor_job);
    CHECK(s.floors[1][floor_offset] == kDoomSky2Surface &&
          s.floors[1][floor_offset + 1] == kDoomSky2Surface);

    /* Width changes cannot reuse a cache lacking the side cameras. */
    s.cached = true;
    DoomRendererConfigure(&fx, true, true, 256);
    CHECK(!s.cached);
    s.cached = true;
    DoomRendererConfigure(&fx, true, true, 448);
    CHECK(!s.cached);

    Ppu *ppu = calloc(1, sizeof(*ppu));
    CHECK(ppu);
    ppu->bgmode = 3;
    ppu->screenEnabled[0] = 1;
    ppu->cgram[5] = 4; /* Stock bit replication produces 33, not scaled 32. */
    SetBrightness(ppu, 15);
    /* The host prepares raster capture before EndSimFrame counts the field
     * in which the native BSP hook captured this new renderer generation. */
    CHECK(s.captured_field == s.field + 1);
    DoomRendererPreparePpu(ppu);
    CHECK(overlay_bindings == 1);
    const unsigned pending_field = s.field;
    s.field = s.captured_field + kMaxHistoryAge + 1;
    CHECK(!Gameplay(ppu));
    s.field = pending_field - 1;
    CHECK(!Gameplay(ppu)); /* Two fields ahead is not the pending field. */
    s.field = pending_field;
    DoomRendererObserveLine(ppu, kNativeViewY + 1, NULL);
    CHECK(s.visible_rows[0] && s.palettes[0][5] == 0xff210000);
    SetBrightness(ppu, 6);
    DoomRendererObserveLine(ppu, kNativeViewY + 2, NULL);
    CHECK(s.palettes[1][5] == 0xff0d0000);

    /* Actual blank rows stay blank, while the guest's VBlank register image
     * must not disable a field that contained a visible world viewport. */
    ppu->inidisp = 0x80;
    DoomRendererObserveLine(ppu, kNativeViewY + 3, NULL);
    CHECK(!s.visible_rows[2]);
    CHECK(Gameplay(ppu));
    uint32_t *output = calloc(342 * 224, sizeof(uint32_t));
    CHECK(output);
    s.has_presented = true;
    CHECK(!DoomRendererDraw(ppu, (uint8_t *)output, 342 * 4, 342, 224, 0));
    CHECK(replay_attempts == 1 && s.stats.failures == 1);
    CHECK(!s.has_presented);

    /* A visible cache hit after a native-only transient presents the cached
     * pose again; the next capture must rebase from that actual image. */
    s.has_previous = false;
    s.cached = true;
    s.cached_generation = s.generation;
    s.cached_camera = s.current;
    s.cached_camera.angle &= 0xffc0;
    s.cached_scene_key = SceneKey(1);
    CHECK(DoomRendererDraw(ppu, (uint8_t *)output, 342 * 4, 342, 224, 0));
    CHECK(s.has_presented && replay_attempts == 1 && s.stats.cache_hits == 1);

    /* Translate the resolved weapon colour and its mask together, leaving
     * the original location covered by the newly rendered world. */
    ppu->renderPitch = 256 * sizeof(uint32_t);
    ppu->renderBuffer = calloc(224, ppu->renderPitch);
    CHECK(ppu->renderBuffer);
    const unsigned gun_y = kNativeViewY;
    const unsigned gun_x = 106;
    s.weapon[gun_y * kPpuBufWidth + kPpuExtraLeftRight + gun_x] = 0xff123456;
    ((uint32_t *)(ppu->renderBuffer + gun_y * ppu->renderPitch))[gun_x] = 0xffabcdef;
    DoomWeaponCapture(&s.weapon_motion, 100, 100, 123, s.field, 6);
    int dx, dy;
    DoomWeaponOffset(&s.weapon_motion, s.field, 0, true, &dx, &dy);
    DoomWeaponCapture(&s.weapon_motion, 106, 100, 123, s.field + 6, 6);
    s.field += 6;
    CHECK(DoomRendererDraw(ppu, (uint8_t *)output, 342 * 4, 342, 224, 0));
    CHECK(output[gun_y * 342 + 43 + 100] == 0xff123456);
    CHECK(output[gun_y * 342 + 43 + gun_x] != 0xff123456);
    CHECK(s.stats.weapon_offset_x == -6);
    /* A complete hidden bottom tile remains visible when translated upward.
     * It clips at the fixed HUD boundary, rather than moving that boundary. */
    s.weapon_tile_count = 1;
    s.weapon_tiles[0].x = 100;
    s.weapon_tiles[0].y = kNativeViewY + DOOM_VIEW_HEIGHT;
    memset(s.weapon_tiles[0].pixels, 1, 64);
    for (unsigned row = 0; row < DOOM_VIEW_HEIGHT; row++) {
        s.visible_rows[row] = true;
        s.weapon_palette[row][1] = 0xff204080;
    }
    DoomWeaponCapture(&s.weapon_motion, 100, 94, 456, s.field, 6);
    DoomWeaponOffset(&s.weapon_motion, s.field, 0, true, &dx, &dy);
    DoomWeaponCapture(&s.weapon_motion, 100, 100, 456, s.field + 6, 6);
    s.field += 6;
    const unsigned edge = kNativeViewY + DOOM_VIEW_HEIGHT;
    output[edge * 342 + 43 + 100] = 0xffaabbcc; /* HUD sentinel. */
    CHECK(DoomRendererDrawWeapon(ppu, (uint8_t *)output, 342 * 4, 342, 224, 0, true));
    CHECK(output[(edge - 6) * 342 + 43 + 100] == 0xff204080);
    CHECK(output[edge * 342 + 43 + 100] == 0xffaabbcc);
    /* Invisibility affects colour only, and disabling interpolation uses the
     * native position immediately. */
    s.weapon_tiles[0].y = edge - 10;
    s.interpolation = false;
    s.weapon_translucent = true;
    output[(edge - 10) * 342 + 43 + 100] = 0xff80a0c0;
    CHECK(DoomRendererDrawWeapon(ppu, (uint8_t *)output, 342 * 4, 342, 224, 0, true));
    CHECK(output[(edge - 10) * 342 + 43 + 100] == 0xff5070a0);
    CHECK(s.stats.weapon_offset_x == 0 && s.stats.weapon_offset_y == 0);
    s.weapon_translucent = false;
    CHECK(DoomRendererDrawWeapon(ppu, (uint8_t *)output, 342 * 4, 342, 224, 0, true));
    CHECK(output[(edge - 10) * 342 + 43 + 100] == 0xff204080);
    /* A muzzle flash is a separate image and can use another OBJ palette. */
    s.weapon_tile_count = 2;
    s.weapon_tiles[1] = s.weapon_tiles[0];
    s.weapon_tiles[1].x = 116;
    s.weapon_tiles[1].palette = 2;
    for (unsigned row = 0; row < DOOM_VIEW_HEIGHT; row++)
        s.weapon_palette[row][2 * 16 + 1] = 0xffffff00;
    CHECK(DoomRendererDrawWeapon(ppu, (uint8_t *)output, 342 * 4, 342, 224, 0, true));
    CHECK(output[(edge - 10) * 342 + 43 + 116] == 0xffffff00);
    CHECK(output[(edge - 10) * 342 + 43 + 100] == 0xff204080);
    free(ppu->renderBuffer);
    ppu->renderBuffer = NULL;

    /* Core/allocator reuse after reset loses its hooks even if every pointer
     * has the same numeric address. Reconfigure must still rearm capture. */
    DoomRendererReset();
    fx.pc_hook_count = 0;
    DoomRendererConfigure(&fx, true, true, 342);
    CHECK(registrations == 2 && fx.pc_hook_count == 1 && s.supported);
    DoomRendererConfigure(&fx, false, false, 256);
    CHECK(!s.hook_installed && !s.has_snapshot);
    s.palettes[0][5] = 0x12345678;
    ppu->inidisp = 0x80;
    DoomRendererObserveLine(ppu, kNativeViewY + 1, NULL);
    CHECK(s.palettes[0][5] == 0x12345678); /* Blank scanout does no palette work. */
    DoomRendererConfigure(NULL, false, false, 256);
    free(output); free(ppu); free(fx.ram); free(fx.rom);
    puts("Doom renderer state tests passed");
    return 0;
}

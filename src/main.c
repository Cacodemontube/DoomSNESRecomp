/*
 * Doom — desktop host shim.
 *
 * Identity and hooks, nothing else. The host itself is the framework's
 * (snesrecomp/runner/src/desktop/host_main.h): the pre-boot launcher, ROM
 * resolution and digest checks, config.ini and keybinds.ini, the window and
 * the SDL / OpenGL presenters, audio, gamepads, the in-game save-state
 * browser and rewind filmstrip, the OSD, the pacing clock, crash handlers and
 * the post-mortem report, mod packages, Generate & rebuild, and the netplay
 * barrier when the project is built with it. A fix there reaches this
 * project on a submodule pull; nothing in this file needs to change for it.
 *
 * This file used to BE the host -- a ~700-line copy made at scaffold time --
 * so every project scaffolded before a fix kept the bug. Two ports of the
 * same game, a week apart, had different hosts.
 *
 * Nothing here ever ships a ROM. The player supplies one; the host makes
 * that easy (launcher, then positional argument, then a copy beside the
 * executable, then the rom.cfg cache, then a file picker) and checks what it
 * is handed against the digests in rom_identity.txt.
 *
 * Grow this file only with what is specific to THIS title: a custom
 * presenter (prepare_frame / draw_frame), an SPC player, a pacing rule
 * (keep_pacing_debt), a Mods provider. See SnesDesktopHostGame for the
 * complete list of hooks and what each is for.
 */

#include "host_main.h"
#include "game_rtl.h"
#include "doom_presentation.h"
#include "doom_debug.h"
#include "doom_input.h"
#include "doom_cheats.h"
#include "doom_episodes.h"
#include "snesrecomp_rom_identity.h"  /* generated from rom_identity.txt */

#ifndef __ANDROID__
#define SDL_MAIN_HANDLED 1
#endif
#include "desktop/sdl_compat.h"
#ifdef __ANDROID__
/* SDLActivity loads libmain.so and dlsyms its SDL_main. */
#define main SDL_main
#endif

#ifndef SNES_GAME_VERSION
#define SNES_GAME_VERSION "dev"
#endif

static void EndFrame(const uint8_t *field, unsigned number) {
    DoomPresentationEnd(field, number);
    DoomDebugRecordFrame(number);
}

static void Reset(void) { DoomCheatsReset(); DoomInputReset(); DoomPresentationReset(); }
static void Shutdown(void) { DoomEpisodesShutdown(); DoomCheatsShutdown(); DoomInputShutdown(); DoomPresentationReset(); }
static void BeforeFrame(void) { DoomEpisodesBeforeFrame(); DoomCheatsBeforeFrame(); DoomInputBeforeFrame(); DoomPresentationBeforeFrame(); }
static int InputEvent(const void *event) { DoomCheatsEvent(event);return DoomInputEvent(event); }
static void InputSuspended(int value) { DoomCheatsSuspended(value);DoomInputSuspended(value); }

static int DrawFrame(uint8_t *dst, size_t pitch, const uint8_t *field,
                     int width, int height, double alpha) {
#if SNESRECOMP_TRACE
    Uint64 start = SDL_GetPerformanceCounter();
#endif
    int result = DoomPresentationDraw(dst, pitch, field, width, height, alpha);
#if SNESRECOMP_TRACE
    DoomDebugRecordDraw(1000.0 * (double)(SDL_GetPerformanceCounter() - start) /
                        (double)SDL_GetPerformanceFrequency());
#endif
    return result;
}

static const SnesDesktopHostGame kGameHost = {
    .display_name        = "Doom",
    .window_title        = "Doom",
    .region              = SNESRECOMP_ROM_REGION,
    .rom_file            = SNESRECOMP_ROM_FILE,
    .expected_sha256_hex = SNESRECOMP_ROM_EXPECTED_SHA256,
    .expected_crc32_hex  = SNESRECOMP_ROM_EXPECTED_CRC32,
    .game_id             = SNESRECOMP_ROM_GAME_ID,
    .build_version       = SNES_GAME_VERSION,
    .game_info           = &kGameInfo,
    .num_players         = 1,
    /* Battery-backed SRAM shows the launcher's SAVES panel. Leave NULL for a
     * title without one. The path is exe-relative. */
    .sram_path           = "saves/save.srm",
    .in_game_launcher    = 1,
    .on_reset            = Reset,
    .on_shutdown         = Shutdown,
    .before_run_frame    = BeforeFrame,
    .prepare_frame       = DoomPresentationPrepare,
    .begin_sim_frame     = DoomPresentationBegin,
    .end_sim_frame       = EndFrame,
    .draw_frame          = DrawFrame,
    .presentation_hz     = DoomPresentationRate,
    .keep_pacing_debt    = DoomPresentationKeepDebt,
    .window_base_width   = DoomPresentationWindowWidth,
    .compute_viewport    = DoomPresentationViewport,
    .keyboard_input      = DoomInputKeyboard,
    .auxiliary_input     = DoomInputAuxiliary,
    .input_event         = InputEvent,
    .input_suspended     = InputSuspended,
};

#ifndef __ANDROID__
#undef main   /* desktop: keep plain main() even if SDL_main.h remapped it */
#endif
int main(int argc, char **argv)
{
    DoomDebugInit();
    return snesrecomp_desktop_main(&kGameHost, argc, argv);
}

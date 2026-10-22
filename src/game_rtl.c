/*
 * Doom — per-game runtime glue.
 *
 * THIS FILE IS THE PORT. Everything else the scaffold produced is layout,
 * build wiring, and packaging; this is where the actual work happens.
 *
 * The framework does not, and cannot, drive an arbitrary SNES game on its
 * own. Two responsibilities always land on the game host:
 *
 *   1. Deciding what "one frame" means for THIS title, and returning from
 *      run_frame() at that boundary. A real ROM's reset vector never
 *      returns — it enters a main loop that waits on vblank — so somebody
 *      has to choose the yield point.
 *   2. Delivering NMI/IRQ at the hardware edge (see
 *      snesrecomp/docs/FRAME_MODEL_HOSTS.md).
 *
 * A new port starts on the framework's beam-aligned driver
 * (runner/src/beam_frame_driver.h): one frame is one PPU field, cut at the
 * beam's vblank edge; interrupts -- raster splits and a coprocessor's IRQ
 * line -- are taken where the beam latches them; parked time still runs the
 * beam, APU and coprocessors; and the field is rasterized from the raster
 * journal with real HDMA. It is the framework's, so a fix to it reaches this
 * port on a submodule pull.
 *
 * This file used to hold its own copy of a simpler driver: frames measured
 * from wherever the CPU last stopped, NMI delivered at an arbitrary beam
 * position, the field drawn with end-of-frame register state. A game that
 * uploads in vblank between INIDISP=$80 and $0F flickered black, and one
 * that chains raster splits fell apart. Replace these calls only for what
 * the driver does not do; MetalWarriorsSNESRecomp's src/mw_rtl.c and
 * GundamWingEndlessDuelSNESRecomp's src/game_rtl.c are developed examples of
 * title-specific drivers.
 */

#include "game_rtl.h"
#include "doom_presentation.h"
#include "doom_renderer.h"
#include "doom_input.h"
#include "doom_cheats.h"
#include "doom_infighting.h"
#include "doom_music.h"
#include "doom_ppu_state.h"

#include "beam_frame_driver.h"
#include "common_rtl.h"
#include "snes/saveload.h"
#include "snes/snes.h"
#include <string.h>

/* The generic SNES blob contains the interpreter CPU, not the native
 * CpuState and beam driver's continuation. Resuming it without these
 * restarts the reset vector over restored gameplay RAM. */
static const uint32_t doom_state_marker_old[2] = {0x4d4f4f44u, 0x335845u};
static const uint32_t doom_state_marker[2] = {0x4d4f4f44u, 0x345845u};
static bool DoomStatePreflight(const void *data, size_t size) {
    return size >= sizeof(doom_state_marker) &&
        !memcmp((const uint8_t *)data + size - sizeof(doom_state_marker),
                doom_state_marker, sizeof(doom_state_marker)) ||
        (size >= sizeof(doom_state_marker_old) &&
         !memcmp((const uint8_t *)data + size - sizeof(doom_state_marker_old),
                 doom_state_marker_old, sizeof(doom_state_marker_old)));
}
static void DoomStateSave(SaveLoadInfo *sli) {
    snes_beam_frame_driver_save_state(sli);
    RtlSaveExecutionState(sli);
    if(!DoomPpuSaveLoadAccess(g_snes->ppu,sli))RtlStateStreamFail(sli);
    DoomRendererSavePauseState(sli);
    DoomInfightingSave(sli);
    uint32_t marker[2];memcpy(marker,doom_state_marker,sizeof(marker));
    sli->func(sli,marker,sizeof(marker));
}
static void DoomStateLoad(SaveLoadInfo *sli, uint32_t version) {
    (void)version;
    bool valid=snes_beam_frame_driver_load_state(sli);
    valid=RtlLoadExecutionState(sli) && valid;
    valid=DoomPpuSaveLoadAccess(g_snes->ppu,sli) && valid;
    valid=DoomRendererLoadPauseState(sli) && valid;
    uint32_t marker[2]={0};
    bool old=sli->peek && sli->peek(sli,0,marker,sizeof(marker)) &&
        !memcmp(marker,doom_state_marker_old,sizeof(marker));
    if(old)DoomInfightingReset();
    else valid=DoomInfightingLoad(sli) && valid;
    sli->func(sli,marker,sizeof(marker));
    if(!valid || memcmp(marker,old?doom_state_marker_old:doom_state_marker,sizeof(marker)))RtlStateStreamFail(sli);
}
static void DoomStateLoaded(uint32_t version) {
    (void)version;
    RtlApplyExecutionState();
    /* The desktop's on_reset clears presentation/input adapters for visible
     * timeline jumps. This hook also runs on speculative rollback, where
     * destroying renderer history and worker threads every field is wrong. */
}

void GameRunOneFrame(void)
{
    if(RtlSpeculativeFrame())DoomInputSpeculativeFrame();
    snes_beam_frame_driver_run_frame();
}

void GameDrawPpuFrame(void)
{
    snes_beam_frame_driver_draw_ppu_frame_observed(DoomRendererObserveLine, NULL);
}

void GameSessionReset(void)
{
    /* Rematch / soft-return: clear anything that must not survive a new
     * session. recomp-ai-rules/NETPLAY.md §3 — sticky state that "has always
     * been fine" is the usual desync culprit, because single-player never
     * re-enters the boot path twice in one process. */
    snes_beam_frame_driver_reset();
    DoomInputReset();
    DoomCheatsReset();
    DoomInfightingReset();
    DoomMusicReset();
    DoomPresentationReset();
}

const RtlGameInfo kGameInfo = {
    .title = "doom",
    .initialize = NULL,
    .run_frame = &GameRunOneFrame,
    .draw_ppu_frame = &GameDrawPpuFrame,
    .save_name_prefix = "save",
    .session_reset = &GameSessionReset,
    .snapshot_preflight = &DoomStatePreflight,
    .state_save_extra = &DoomStateSave,
    .state_load_extra = &DoomStateLoad,
    .on_state_loaded = &DoomStateLoaded,
    .exec_state_bound = DoomInputRollbackBound,
    .exec_state_save = DoomInputRollbackSave,
    .exec_state_load = DoomInputRollbackLoad,
};

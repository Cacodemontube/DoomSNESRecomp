/* Reversible, in-memory USA retail CPU patches. The original DOOM-FX
 * useIMAGINEER condition removes both of these difficulty gates. */
#include "doom_episodes.h"
#include "common_cpu_infra.h"
#include "mod_runtime.h"
#include "snes/cart.h"
#include <string.h>

static int enabled;
static uint8_t *patched_rom;
enum { MenuInit=0x1fa814, EpisodeGate=0x1ffb46 };
static const uint8_t menu_original[]={
    0xa9,0x01,0xa0,0x5f,0x48,0xae,0xee,0x06,0xe0,0x02,0x00,
    0x90,0x0d,0x1a,0xa0,0x6f,0x38,0xe0,0x03,0x00,0x90,0x04,
    0x1a,0xa0,0x7f,0x28,0x85,0x8a
};
static const uint8_t gate_original[]={
    0xad,0x36,0x07,0xc9,0x02,0xb0,0x0b,0xc0,0x12,0xf0,0x69,
    0x3a,0xf0,0x04,0xc0,0x09,0xf0,0x62,0xc2,0x20
};
static void Settings(void) {
    enabled=snes_mod_runtime_feature_enabled_c("doom.episodes","unlocked-episodes");
}
SNES_MOD_CONSTRUCTOR(RegisterDoomEpisodes) {
    snes_mod_register_activation_plugin("doom.episodes.unlocked",Settings);
    snes_mod_register_reset_callback(Settings);
}
static uint8_t *CurrentRom(void) {
    Cart *cart=g_snes?g_snes->cart:NULL;
    return cart && cart->rom && cart->romSize==0x200000?cart->rom:NULL;
}
static void Restore(uint8_t *rom) {
    rom[MenuInit+1]=menu_original[1];
    rom[MenuInit+2]=menu_original[2];
    rom[MenuInit+3]=menu_original[3];
    rom[EpisodeGate+5]=gate_original[5];
}
void DoomEpisodesBeforeFrame(void) {
    Settings();
    uint8_t *rom=CurrentRom();
    if(patched_rom!=rom)patched_rom=NULL;
    if(!enabled) {
        if(patched_rom)Restore(patched_rom);
        patched_rom=NULL;return;
    }
    if(!rom || patched_rom)return;
    if(memcmp(rom+MenuInit,menu_original,sizeof(menu_original)) ||
       memcmp(rom+EpisodeGate,gate_original,sizeof(gate_original)))return;
    /* LDA #3; BRA to the full-height LDY. Both the selectable item count
     * and the menu's HDMA reveal region must include all three episodes. */
    rom[MenuInit+1]=3;rom[MenuInit+2]=0x80;rom[MenuInit+3]=0x13;
    /* BCS -> BRA to native inventory/score/next-episode handling. The
     * original SkillLevel load stays intact; no difficulty is substituted. */
    rom[EpisodeGate+5]=0x80;
    patched_rom=rom;
}
void DoomEpisodesShutdown(void) {
    if(patched_rom && patched_rom==CurrentRom())Restore(patched_rom);
    patched_rom=NULL;
}

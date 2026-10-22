#include "doom_crosshair.h"
#include "doom_renderer.h"
#include "mod_runtime.h"
static int enabled;
static void Settings(void) {
    enabled=snes_mod_runtime_feature_enabled_c("doom.crosshair","autoaim-crosshair");
}
SNES_MOD_CONSTRUCTOR(RegisterDoomCrosshair) {
    snes_mod_register_presentation_plugin("doom.crosshair.autoaim",Settings);
    snes_mod_register_reset_callback(Settings);
}
void DoomCrosshairDraw(uint8_t *dst,size_t pitch,unsigned width,unsigned height) {
    if(enabled)DoomRendererDrawCrosshair(dst,pitch,width,height);
}

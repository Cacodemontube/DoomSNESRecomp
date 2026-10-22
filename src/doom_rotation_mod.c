#include "doom_rotations.h"
#include "mod_runtime.h"
#include "desktop/sdl_compat.h"
#include <stdio.h>
static void Activate(void) {
    char wad[4096],directory[4096],error[512];
    DoomRotationsClear();
    if(!snes_mod_runtime_resource_path_c("doom.rotations","pc-rotations","doom-wad",wad,sizeof(wad)))return;
    const char *base=SDL_GetBasePath();
    int n=snprintf(directory,sizeof(directory),"%sassets/pc-rotations",base?base:"./");
    if(n<0 || n>=(int)sizeof(directory) || !DoomRotationsImport(wad,directory,error,sizeof(error)))
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"PC enemy rotations",
            n<0 || n>=(int)sizeof(directory)?"The build directory path is too long":error,NULL);
}
SNES_MOD_CONSTRUCTOR(RegisterDoomRotations) {
    snes_mod_register_presentation_plugin("doom.rotations.pc",Activate);
    snes_mod_register_reset_callback(DoomRotationsClear);
}

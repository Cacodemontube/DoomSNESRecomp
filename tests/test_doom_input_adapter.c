/* Production SDL adapter and real GSU hook execution, with deterministic
 * keyboard/focus/mouse sources rather than driving a user's desktop. */
#define SDL_MAIN_HANDLED 1
#include "desktop/sdl_compat.h"
static SDL_Window *test_focus;
static uint32_t test_buttons;
static unsigned captures, releases;
static SDL_Window *TestFocus(void) { return test_focus; }
static uint32_t TestMouse(void *x, void *y) { (void)x;(void)y;return test_buttons; }
#if SNESRECOMP_SDL3
static bool TestCapture(SDL_Window *window, bool on) {
    (void)window; if(on)captures++;else releases++;return true;
}
#else
static int TestCapture(SDL_bool on) { if(on)captures++;else releases++;return 0; }
#endif
#define SDL_GetKeyboardFocus TestFocus
#define SDL_GetMouseState TestMouse
#if SNESRECOMP_SDL3
#define SDL_SetWindowRelativeMouseMode TestCapture
#else
#define SDL_SetRelativeMouseMode TestCapture
#endif
#include "../src/doom_input.c"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1); } } while(0)
Snes *g_snes;
static int selected;
static const char *invert="off", *look="on";
int snes_mod_runtime_feature_enabled_c(const char *package,const char *feature) {
    CHECK(!strcmp(package,"doom.presentation")&&!strcmp(feature,"modern-controls"));
    return selected;
}
int snes_mod_runtime_feature_option_value_c(const char *package,const char *feature,
    const char *option,char *out,uint32_t size) {
    (void)package;(void)feature;
    const char *value=!strcmp(option,"invert-y")?invert:!strcmp(option,"vertical-look")?look:"1";
    CHECK(size>strlen(value));strcpy(out,value);return 1;
}
int snes_mod_register_activation_plugin(const char *id,SNESModActivationCallback cb) {
    if(strcmp(id,"doom.controls.modern")||!cb)abort();return 1;
}
int snes_mod_register_reset_callback(SNESModActivationCallback cb) { if(!cb)abort();return 1; }
static void Move(SuperFx *fx) {
    superfx_cpu_write_io(fx,0x301e,0xc1);
    superfx_cpu_write_io(fx,0x301f,0x81);
    superfx_sync(fx,fx->master_clock+100000);
    CHECK(!superfx_is_running(fx));
}
int main(void) {
    uint8_t *rom=calloc(1,0x200000),*ram=calloc(1,0x10000);CHECK(rom&&ram);
    const uint8_t entry[]={0x29,0x10,0xa1,0x20,0x71,0x09,0x07,0x01};
    const uint8_t load[]={0xa0,0x14,0x3d,0xa1,0xc6,0x51,0x40,0x56,0x3f,0x71,0x90};
    memcpy(rom+0x1c1,entry,sizeof(entry));memcpy(rom+0x23b,load,sizeof(load));
    SuperFx *fx=superfx_create(rom,0x200000,ram,0x10000);CHECK(fx);
    Snes machine={0};Cart cart={0};machine.cart=&cart;cart.superfx=fx;g_snes=&machine;
    ram[0x18c]=0x8e;ram[0x18d]=0x5a;
    uint8_t keys[SDL_SCANCODE_COUNT]={0};test_focus=(SDL_Window*)1;
    keys[SDL_SCANCODE_W]=keys[SDL_SCANCODE_A]=keys[SDL_SCANCODE_LSHIFT]=1;
    CHECK(DoomInputKeyboard(keys,0,0x123)==0x123);
    DoomInputBeforeFrame();CHECK(!fx->pc_hook_count);
    selected=1;DoomInputBeforeFrame();CHECK(fx->pc_hook_count==1);
    CHECK(DoomInputKeyboard(keys,0,0)==0x411);
    CHECK(DoomInputKeyboard(keys,1,0x123)==0x123);
    Move(fx);CHECK(gameplay);
    CHECK(DoomInputKeyboard(keys,0,0)==0x411);CHECK(captures==1);
    SDL_Event event={0};event.type=SDL_MOUSEMOTION;event.motion.xrel=10;event.motion.yrel=-40;
    CHECK(DoomInputEvent(&event));CHECK(DoomInputPitch()==10);
    Move(fx);CHECK((ram[0x5aa2]|(ram[0x5aa3]<<8))==0xfd80);CHECK(yaw_counts==0);
    Move(fx);CHECK((ram[0x5aa2]|(ram[0x5aa3]<<8))==0xfd80);
    test_buttons=SDL_BUTTON_LMASK;CHECK(DoomInputKeyboard(keys,0,0)==0x413);
    event.type=SDL_KEYDOWN;
#if SNESRECOMP_SDL3
    event.key.key=SDLK_RETURN;event.key.mod=0;
#else
    event.key.keysym.sym=SDLK_RETURN;event.key.keysym.mod=0;
#endif
    CHECK(DoomInputEvent(&event));keys[SDL_SCANCODE_RETURN]=1;
    CHECK(!captured_window);
    CHECK(DoomInputKeyboard(keys,0,0)==0x419);
    event.type=SDL_MOUSEBUTTONDOWN;event.button.button=SDL_BUTTON_LEFT;DoomInputEvent(&event);
    CHECK(!(DoomInputKeyboard(keys,0,0)&2)); /* recapture click must not fire */
    event.type=SDL_KEYDOWN;
#if SNESRECOMP_SDL3
    event.key.key=SDLK_l;event.key.mod=KMOD_CTRL;
#else
    event.key.keysym.sym=SDLK_l;event.key.keysym.mod=KMOD_CTRL;
#endif
    CHECK(!DoomInputEvent(&event));
#if SNESRECOMP_SDL3
    event.key.key=SDLK_ESCAPE;event.key.mod=0;
#else
    event.key.keysym.sym=SDLK_ESCAPE;event.key.keysym.mod=0;
#endif
    CHECK(DoomInputEvent(&event));CHECK(!captured_window);
    DoomInputKeyboard(keys,0,0);CHECK(!captured_window);
    event.type=SDL_MOUSEBUTTONDOWN;event.button.button=SDL_BUTTON_LEFT;CHECK(DoomInputEvent(&event));
    CHECK(captured_window);DoomInputSuspended(1);CHECK(!captured_window);
    CHECK(DoomInputKeyboard(keys,0,0)==0);DoomInputSuspended(0);
    CHECK(DoomInputKeyboard(keys,0,0)==0);CHECK(!captured_window);
#if SNESRECOMP_SDL3
    event.type=SDL_EVENT_WINDOW_FOCUS_LOST;
#else
    event.type=SDL_WINDOWEVENT;event.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
#endif
    DoomInputEvent(&event);CHECK(!captured_window);
    invert="on";event.type=SDL_MOUSEBUTTONDOWN;event.button.button=SDL_BUTTON_LEFT;DoomInputEvent(&event);
    event.type=SDL_MOUSEMOTION;event.motion.xrel=0;event.motion.yrel=-40;DoomInputEvent(&event);
    CHECK(DoomInputPitch()==0);
    look="off";DoomInputBeforeFrame();CHECK(!DoomInputLookEnabled()&&DoomInputPitch()==0);
    for(unsigned i=0;i<13;i++)DoomInputBeforeFrame();CHECK(!captured_window&&yaw_counts==0);
    selected=0;DoomInputBeforeFrame();CHECK(!fx->pc_hook_count);
    CHECK(DoomInputKeyboard(keys,0,0x123)==0x123);
    selected=1;rom[0x1c1]^=1;DoomInputBeforeFrame();CHECK(!fx->pc_hook_count);
    DoomInputSuspended(1);DoomInputReset();CHECK(suspended);
    DoomInputSuspended(0);CHECK(DoomInputKeyboard(keys,0,0)==0);
    memset(keys,0,sizeof(keys));DoomInputKeyboard(keys,0,0);
    keys[SDL_SCANCODE_W]=1;CHECK(DoomInputKeyboard(keys,0,0)==0x10);
    DoomInputShutdown();superfx_destroy(fx);free(rom);free(ram);
    puts("Input adapter: real GSU hook, concurrent strafe/turn, capture, focus, pause, disable and ROM guards passed");
    return 0;
}

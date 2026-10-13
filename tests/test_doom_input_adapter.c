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
static void TestSize(SDL_Window *w,int *x,int *y) { (void)w;*x=256;*y=224; }
#define SDL_GetWindowSize TestSize
#include "../src/doom_input.c"
static int test_native_menu;
int DoomNativeMenuActive(void) { return test_native_menu; }
int DoomPresentationMapPointer(double x,double y,int w,int h,double *nx,double *ny) { (void)w;(void)h;*nx=x;*ny=y;return 1; }
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1); } } while(0)
Snes *g_snes;
static int selected;
static const char *invert="off", *look="off", *jump="off", *crouch="off", *pointer="off";
int snes_mod_runtime_feature_enabled_c(const char *package,const char *feature) {
    CHECK(!strcmp(package,"doom.presentation")&&!strcmp(feature,"modern-controls"));
    return selected;
}
int snes_mod_runtime_feature_option_value_c(const char *package,const char *feature,
    const char *option,char *out,uint32_t size) {
    (void)package;(void)feature;
    const char *value=!strcmp(option,"invert-y")?invert:!strcmp(option,"vertical-look")?look:!strcmp(option,"jumping")?jump:!strcmp(option,"crouching")?crouch:!strcmp(option,"menu-pointer")?pointer:"1";
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
    selected=1;ReadSettings();
    CHECK(!look_enabled && !jump_enabled);
    look="on";selected=0;
    SDL_Event pause={0};pause.type=SDL_KEYDOWN;
#if SNESRECOMP_SDL3
    pause.key.key=SDLK_p;
#else
    pause.key.keysym.sym=SDLK_p;
#endif
    CHECK(DoomInputEvent(&pause));
    pause.type=SDL_KEYUP;CHECK(DoomInputEvent(&pause));
    SDL_Event renderer={0};renderer.type=SDL_KEYDOWN;
#if SNESRECOMP_SDL3
    renderer.key.key=SDLK_r;
#else
    renderer.key.keysym.sym=SDLK_r;
#endif
    CHECK(DoomInputEvent(&renderer));
    renderer.type=SDL_KEYUP;CHECK(DoomInputEvent(&renderer));
#if SNESRECOMP_SDL3
    renderer.key.mod=KMOD_CTRL;
#else
    renderer.key.keysym.mod=KMOD_CTRL;
#endif
    renderer.type=SDL_KEYDOWN;CHECK(!DoomInputEvent(&renderer));
    renderer.type=SDL_KEYUP;CHECK(!DoomInputEvent(&renderer));
    uint8_t *rom=calloc(1,0x200000),*ram=calloc(1,0x10000);CHECK(rom&&ram);
    const uint8_t entry[]={0x29,0x10,0xa1,0x20,0x71,0x09,0x07,0x01};
    const uint8_t load[]={0xa0,0x14,0x3d,0xa1,0xc6,0x51,0x40,0x56,0x3f,0x71,0x90};
    memcpy(rom+0x1c1,entry,sizeof(entry));memcpy(rom+0x23b,load,sizeof(load));
    const uint8_t fall[]={0xa0,0x22,0x3d,0xa2,0xc6,0x52,0x11,0x40};
    const uint8_t gravity[]={0xb1,0x66,0x3f,0x62,0x06,0x02,0x20,0xb2,0x90,0x9b,0x01};
    memcpy(rom+0x1ac,fall,sizeof(fall));memcpy(rom+0x12c8,gravity,sizeof(gravity));
    const uint8_t eye[]={0xa1,0x2a,0x56,0x3f,0x61,0x07,0x02,0x01,0xb1,0x90};
    const uint8_t clearance[]={0xa6,0x38,0x21,0x56,0x3f,0x61,0x07,0x08};
    memcpy(rom+0x38a,eye,sizeof(eye));memcpy(rom+0x2521,clearance,sizeof(clearance));
    const uint8_t ceiling_object[]={0xef,0x51,0x3f,0x63,0x07,0xdc};
    const uint8_t ceiling_player[]={0xa6,0x38,0x21,0x56,0x61,0x07,0xaa};
    memcpy(rom+0x5e4e,ceiling_object,sizeof(ceiling_object));
    memcpy(rom+0x5e7f,ceiling_player,sizeof(ceiling_player));
    SuperFx *fx=superfx_create(rom,0x200000,ram,0x10000);CHECK(fx);
    Snes machine={0};Cart cart={0};machine.cart=&cart;cart.superfx=fx;g_snes=&machine;
    ram[0x18c]=0x8e;ram[0x18d]=0x5a;
    uint8_t keys[SDL_SCANCODE_COUNT]={0};test_focus=(SDL_Window*)1;
    keys[SDL_SCANCODE_W]=keys[SDL_SCANCODE_A]=keys[SDL_SCANCODE_LSHIFT]=1;
    CHECK(DoomInputKeyboard(keys,0,0x123)==0x123);
    DoomInputBeforeFrame();CHECK(!fx->pc_hook_count);
    selected=1;DoomInputBeforeFrame();CHECK(fx->pc_hook_count==3);
    /* Absolute hover selects native menu items; off restores keyboard/skull behavior. */
    uint8_t cpu_ram[256]={0};machine.ram=cpu_ram;
    const uint8_t menu_signature[]={0x0a,0x0a,0x0a,0x69,0x14,0x00,0xa8,0xb7,0x82};
    memcpy(rom+0x1fa6e4,menu_signature,sizeof(menu_signature));
    cpu_ram[0x83]=0xf0;cpu_ram[0x8a]=4;rom[0x7000+13]=4;
    for(unsigned i=0;i<4;i++) { rom[0x7000+14+i*8+6]=64;rom[0x7000+14+i*8+7]=56+i*18; }
    pointer="on";ReadSettings();test_native_menu=1;
    CHECK(DoomInputMenuPosition(100,82,0));CHECK(cpu_ram[0x86]==1);
    double mx,my;CHECK(DoomInputMenuPointer(&mx,&my));CHECK(mx==100 && my==82);
    CHECK(DoomInputMenuPosition(100,118,1));CHECK(cpu_ram[0x86]==3 && menu_click);
    menu_click=0;CHECK(DoomInputMenuPosition(10,10,1));CHECK(!menu_click && cpu_ram[0x86]==3);
    pointer="off";ReadSettings();CHECK(!DoomInputMenuPointer(&mx,&my));
    CHECK(!DoomInputMenuPosition(100,64,1));test_native_menu=0;machine.ram=NULL;
    pause.type=SDL_KEYDOWN;CHECK(DoomInputEvent(&pause));
    SDL_Event menu={0};menu.type=SDL_MOUSEMOTION;menu.motion.yrel=48;
    CHECK(DoomInputEvent(&menu));
    CHECK(DoomInputAuxiliary(0,0)==(1u<<5));CHECK(DoomInputAuxiliary(0,0)==(1u<<5));
    field+=2;CHECK(DoomInputAuxiliary(0,0)==0);field+=2;
    CHECK(DoomInputAuxiliary(0,0)==(1u<<5));field+=2;CHECK(DoomInputAuxiliary(0,0)==0);field+=2;
    menu.motion.yrel=-24;DoomInputEvent(&menu);
    CHECK(DoomInputAuxiliary(0,0)==(1u<<4));field+=2;CHECK(DoomInputAuxiliary(0,0)==0);field+=2;
    menu.type=SDL_MOUSEBUTTONDOWN;menu.button.button=SDL_BUTTON_LEFT;DoomInputEvent(&menu);
    CHECK(DoomInputAuxiliary(0,0)==(1u<<8));field+=2;CHECK(DoomInputAuxiliary(0,0)==0);field+=2;
    CHECK(DoomInputKeyboard(keys,0,0)==0x411);
    CHECK(DoomInputKeyboard(keys,1,0x123)==0x123);
    Move(fx);CHECK(gameplay);
    /* Ctrl retains movement while lowering real eye and passage clearance.
     * Releasing under a low ceiling must not stand through that ceiling. */
    uint8_t crouching[SDL_SCANCODE_COUNT]={0};
    ram[0x5a8e + 36]=128;
    crouching[SDL_SCANCODE_LCTRL]=crouching[SDL_SCANCODE_W]=1;
    CHECK(DoomInputKeyboard(crouching,0,0)==0);
    CHECK(!DoomInputRequestCrouch(1));Move(fx);CHECK(!crouch_active);
    crouch="on";
    CHECK(DoomInputKeyboard(crouching,0,0)==0x10);
    DoomInputBeforeFrame();CHECK(fx->pc_hook_count==7);
    Move(fx);CHECK(crouch_active);
    test_buttons=SDL_BUTTON_LMASK;
    CHECK(DoomInputKeyboard(crouching,0,0)==0x12);test_buttons=0;
    superfx_set_reg(fx,1,42);CrouchEye(fx,kEyeClamp,NULL);CHECK(superfx_reg(fx,1)==21);
    superfx_set_reg(fx,6,56);CrouchClearance(fx,kMoveClearance,NULL);CHECK(superfx_reg(fx,6)==28);
    superfx_set_reg(fx,12,0x5a8e);superfx_set_reg(fx,0,56);
    CrouchCeiling(fx,kCeilingObjectHeight,NULL);CHECK(superfx_reg(fx,0)==28);
    superfx_set_reg(fx,12,0x5ab4);superfx_set_reg(fx,0,64);
    CrouchCeiling(fx,kCeilingObjectHeight,NULL);CHECK(superfx_reg(fx,0)==64);
    superfx_set_reg(fx,6,56);CrouchCeiling(fx,kCeilingPlayerHeight,NULL);CHECK(superfx_reg(fx,6)==28);
    crouching[SDL_SCANCODE_LCTRL]=0;ram[0x5a8e + 36]=40;
    DoomInputKeyboard(crouching,0,0);Move(fx);CHECK(crouch_active);
    ram[0x5a8e + 36]=128;Move(fx);CHECK(!crouch_active);
    DoomInputBeforeFrame();CHECK(fx->pc_hook_count==3);
    ram[kPlayerHeight]=21;ram[0x5a8e + 36]=40;crouch_active=0;
    DoomInputBeforeFrame();CHECK(crouch_active && fx->pc_hook_count==7);
    ram[kPlayerHeight]=42;ram[0x5a8e + 36]=128;Move(fx);
    DoomInputBeforeFrame();CHECK(!crouch_active && fx->pc_hook_count==3);
    crouch="off";DoomInputKeyboard(crouching,0,0);DoomInputBeforeFrame();
    CHECK(!crouch_enabled && !crouch_active && fx->pc_hook_count==3);
    CHECK(!DoomInputRequestCrouch(1));
    /* Zoom maps to native fire/cycle only while the map is open. Wheel
     * requests survive presentation polls until the native mover consumes them. */
    uint8_t zoom_keys[SDL_SCANCODE_COUNT]={0};ram[0x1e6]=1;
    zoom_keys[SDL_SCANCODE_KP_PLUS]=1;
    CHECK(DoomInputKeyboard(zoom_keys,0,0)==2);
    zoom_keys[SDL_SCANCODE_KP_PLUS]=0;zoom_keys[SDL_SCANCODE_MINUS]=1;
    CHECK(DoomInputKeyboard(zoom_keys,0,0)==0x200);
    CHECK(DoomInputAuxiliary(0,0x200)==0x200);
    superfx_set_reg(fx,9,0x40);Move(fx);CHECK(superfx_reg(fx,9)==0x40);
    SDL_Event wheel={0};wheel.type=SDL_MOUSEWHEEL;wheel.wheel.y=1;
    CHECK(DoomInputEvent(&wheel));CHECK(map_wheel==3);
    CHECK(DoomInputAuxiliary(0,0)==2);CHECK(map_wheel==3);
    Move(fx);CHECK(map_wheel==2 && (superfx_reg(fx,9)&0x60)==0x20);
    wheel.wheel.direction=SDL_MOUSEWHEEL_FLIPPED;wheel.wheel.y=2;
    CHECK(DoomInputEvent(&wheel));CHECK(map_wheel==-4);
    Move(fx);CHECK(map_wheel==-3 && (superfx_reg(fx,9)&0x60)==0x40);
    ram[0x1e6]=0;CHECK(!DoomInputEvent(&wheel));DoomInputAuxiliary(0,0);CHECK(!map_wheel);
    /* A speculative jump/input update must restore exactly at rollback. */
    DoomInputRollback saved;CHECK(DoomInputRollbackSave(&saved,sizeof(saved))==sizeof(saved));
    unsigned real_field=field;DoomInputSpeculativeFrame();jump_active=1;jump_z=25;
    yaw_counts=17;map_wheel=4;crouch_active=1;
    CHECK(DoomInputRollbackLoad(&saved,sizeof(saved)));
    CHECK(field==real_field && !jump_active && jump_z==0 && yaw_counts==0 && !map_wheel && !crouch_active);
    CHECK(!DoomInputRollbackLoad(&saved,sizeof(saved)-1));
    CHECK(DoomInputKeyboard(keys,0,0)==0x411);CHECK(captures==1);
    uint8_t number_keys[SDL_SCANCODE_COUNT]={0};
    const uint8_t expected_weapons[]={0,2,4,8,10,12,14};
    for(unsigned slot=0;slot<7;slot++) {
        memset(number_keys,0,sizeof(number_keys));DoomInputKeyboard(number_keys,0,0);
        ram[kPlayerArms]=0;ram[kWeaponNext]=2;
        number_keys[SDL_SCANCODE_1+slot]=1;
        DoomInputKeyboard(number_keys,0,0);Move(fx);
        CHECK(ram[kWeaponNext]==(slot ? 2 : 0)); /* unavailable weapons ignored */
        number_keys[SDL_SCANCODE_1+slot]=0;DoomInputKeyboard(number_keys,0,0);
        ram[kPlayerArms]=(uint8_t)(1u<<(expected_weapons[slot]/2));
        number_keys[SDL_SCANCODE_1+slot]=1;
        DoomInputKeyboard(number_keys,0,0);Move(fx);
        CHECK(ram[kWeaponNext]==expected_weapons[slot] && !ram[kWeaponNext+1]);
        ram[kWeaponNext]=2;DoomInputKeyboard(number_keys,0,0);Move(fx);
        CHECK(ram[kWeaponNext]==2); /* a held key never restarts the switch */
    }
    memset(number_keys,0,sizeof(number_keys));DoomInputKeyboard(number_keys,0,0);
    ram[kPlayerArms]=8;number_keys[SDL_SCANCODE_1]=1;
    DoomInputKeyboard(number_keys,0,0);Move(fx);CHECK(ram[kWeaponNext]==6);
    memset(number_keys,0,sizeof(number_keys));DoomInputKeyboard(number_keys,0,0);
    number_keys[SDL_SCANCODE_1]=number_keys[SDL_SCANCODE_LCTRL]=1;
    ram[kWeaponNext]=2;DoomInputKeyboard(number_keys,0,0);Move(fx);
    CHECK(ram[kWeaponNext]==2);
    DoomInputKeyboard(keys,0,0);
    CHECK(DoomInputAuxiliary(0,0x210)==0x10); /* modern cycle suppressed */
    uint8_t jumping[SDL_SCANCODE_COUNT]={0};jumping[SDL_SCANCODE_SPACE]=1;
    ram[0x5a8e + 36]=128;ram[0x5a8e + 37]=0;
    DoomInputKeyboard(jumping,0,0);Move(fx);
    CHECK(!jump_active && !jump_queued && !DoomInputRequestJump());
    jumping[SDL_SCANCODE_SPACE]=0;DoomInputKeyboard(jumping,0,0);
    jump="on";jumping[SDL_SCANCODE_SPACE]=1;
    CHECK(!(DoomInputKeyboard(jumping,0,0)&(1u<<9)));Move(fx);CHECK(jump_active);
    int peak=0;
    for(unsigned tick=0;tick<60 && jump_active;tick++) {
        DoomInputBeforeFrame();Move(fx);
        superfx_set_reg(fx,12,0x5a8e);superfx_set_reg(fx,1,ram[0x5a8e + 14]);superfx_set_reg(fx,2,0);
        JumpGravity(fx,kObjectGravity,NULL);
        int z=(int16_t)superfx_reg(fx,1)-(int16_t)superfx_reg(fx,6);
        CHECK(z>=0);if(z>peak)peak=z;ram[0x5a8e + 14]=(uint8_t)z;
        DoomInputKeyboard(jumping,0,0);CHECK(!jump_queued);
    }
    CHECK(!jump_active && peak>=30 && peak<=33 && ram[0x5a8e + 14]==0);
    jumping[SDL_SCANCODE_SPACE]=0;DoomInputKeyboard(jumping,0,0);
    ram[0x5a8e + 36]=64;jumping[SDL_SCANCODE_SPACE]=1;
    DoomInputKeyboard(jumping,0,0);Move(fx);CHECK(jump_active);
    for(unsigned tick=0;tick<60 && jump_active;tick++) {
        DoomInputBeforeFrame();Move(fx);
        superfx_set_reg(fx,12,0x5a8e);superfx_set_reg(fx,1,ram[0x5a8e + 14]);superfx_set_reg(fx,2,0);
        JumpGravity(fx,kObjectGravity,NULL);
        int z=(int16_t)superfx_reg(fx,1)-(int16_t)superfx_reg(fx,6);
        CHECK(z>=0 && z<=8);ram[0x5a8e + 14]=(uint8_t)z;
    }
    CHECK(!jump_active && !ram[0x5a8e + 14]);DoomInputKeyboard(keys,0,0);
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
    invert="on";DoomInputBeforeFrame();event.type=SDL_MOUSEBUTTONDOWN;event.button.button=SDL_BUTTON_LEFT;DoomInputEvent(&event);
    event.type=SDL_MOUSEMOTION;event.motion.xrel=0;event.motion.yrel=-40;DoomInputEvent(&event);
    CHECK(DoomInputPitch()==0);
    look="off";DoomInputBeforeFrame();CHECK(!DoomInputLookEnabled()&&DoomInputPitch()==0);
    for(unsigned i=0;i<13;i++)DoomInputBeforeFrame();CHECK(!captured_window&&yaw_counts==0);
    selected=0;DoomInputBeforeFrame();CHECK(!fx->pc_hook_count);
    CHECK(DoomInputAuxiliary(0,0x210)==0x210); /* controller cycling unchanged */
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

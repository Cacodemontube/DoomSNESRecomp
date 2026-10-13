#include "doom_input.h"
#include "doom_input_math.h"
#include "doom_music.h"
#include "doom_presentation.h"
#include "common_cpu_infra.h"
#include "mod_runtime.h"
#include "snes/cart.h"
#include "snes/superfx.h"
#define SDL_MAIN_HANDLED 1
#include "desktop/sdl_compat.h"
#include <string.h>

static int enabled, look_enabled, jump_enabled, crouch_enabled, invert_y, suspended, released;
static double sensitivity = 1, yaw_counts, pitch;
static SDL_Window *captured_window;
static SuperFx *hook_core;
static unsigned field, last_movement;
static int gameplay;
static int menu_pointer_enabled, menu_pointer_seen, menu_cursor_hidden;
static double menu_pointer_x, menu_pointer_y;
static unsigned movement_updates, turns;
static uint32_t blocked_buttons;
static uint8_t blocked_keys[SDL_SCANCODE_COUNT];
static int block_keys_next;
static int queued_weapon = -1;
static unsigned weapon_keys;
static double menu_motion;
static int menu_steps, menu_click, menu_release;
static uint16_t menu_pad;
static unsigned menu_until;
static int jump_queued, jump_active, jump_key;
static double jump_z,jump_velocity;
static unsigned jump_field;
static int crouch_key, crouch_active, map_wheel;
static int crouch_hooks;
static unsigned weapon_requests,weapon_selections;
enum { kPlayerMovement = 0x81c1, kPlayerPointer = 0x18c, kPlayerAngle = 20 };
enum { kPlayerArms = 0x1d9, kWeaponNext = 0x1e4 };
enum { kPlayerFallCheck=0x81ac,kObjectGravity=0x92c8 };
enum { kEyeClamp=0x838c,kMoveClearance=0xa523,kPlayerHeight=0x30 };
enum { kCeilingObjectHeight=0xde4f,kCeilingPlayerHeight=0xde81 };
static int RamSigned(SuperFx *fx,unsigned address) {
    return (int16_t)(superfx_ram_peek(fx,address)|(superfx_ram_peek(fx,address+1)<<8));
}
typedef struct DoomInputRollback {
    double yaw, z, velocity;
    unsigned field, last_movement, jump_field, movement_updates, turns, weapon_selections;
    int gameplay, weapon, jump_queued, jump_active, crouch_active, map_wheel;
} DoomInputRollback;
size_t DoomInputRollbackBound(void) { return sizeof(DoomInputRollback); }
size_t DoomInputRollbackSave(void *data,size_t capacity) {
    if(!data || capacity<sizeof(DoomInputRollback))return 0;
    DoomInputRollback state={yaw_counts,jump_z,jump_velocity,field,last_movement,jump_field,
        movement_updates,turns,weapon_selections,gameplay,queued_weapon,jump_queued,jump_active,
        crouch_active,map_wheel};
    memcpy(data,&state,sizeof(state));return sizeof(state);
}
int DoomInputRollbackLoad(const void *data,size_t size) {
    if(!data || size!=sizeof(DoomInputRollback))return false;
    DoomInputRollback state;memcpy(&state,data,size);
    yaw_counts=state.yaw;jump_z=state.z;jump_velocity=state.velocity;
    field=state.field;last_movement=state.last_movement;jump_field=state.jump_field;
    movement_updates=state.movement_updates;turns=state.turns;weapon_selections=state.weapon_selections;
    gameplay=state.gameplay;queued_weapon=state.weapon;jump_queued=state.jump_queued;
    jump_active=state.jump_active;crouch_active=state.crouch_active;map_wheel=state.map_wheel;
    return true;
}
void DoomInputSpeculativeFrame(void) { ++field; }
static int MapOpen(void) { return hook_core && RamSigned(hook_core,0x1e6)!=0; }
static void CrouchEye(SuperFx *fx,uint32_t pc,void *context) {
    (void)pc;(void)context;
    if(enabled && !suspended && crouch_active)superfx_set_reg(fx,1,21);
}
static void CrouchClearance(SuperFx *fx,uint32_t pc,void *context) {
    (void)pc;(void)context;
    if(enabled && !suspended && crouch_active)superfx_set_reg(fx,6,28);
}
static void CrouchCeiling(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;
    if(!enabled || suspended || !crouch_active)return;
    if(pc==kCeilingPlayerHeight)superfx_set_reg(fx,6,28);
    else if(superfx_reg(fx,12)==(uint16_t)RamSigned(fx,kPlayerPointer))
        superfx_set_reg(fx,0,28);
}
static void RemoveHooks(SuperFx *fx) {
    superfx_set_pc_hook(fx,kPlayerMovement,NULL,NULL);
    superfx_set_pc_hook(fx,kPlayerFallCheck,NULL,NULL);
    superfx_set_pc_hook(fx,kObjectGravity,NULL,NULL);
    superfx_set_pc_hook(fx,kEyeClamp,NULL,NULL);
    superfx_set_pc_hook(fx,kMoveClearance,NULL,NULL);
    superfx_set_pc_hook(fx,kCeilingObjectHeight,NULL,NULL);
    superfx_set_pc_hook(fx,kCeilingPlayerHeight,NULL,NULL);
    crouch_hooks=0;
}

static void ReadSettings(void) {
    char value[32];
    enabled = snes_mod_runtime_feature_enabled_c("doom.presentation", "modern-controls");
    menu_pointer_enabled = enabled;
    if(snes_mod_runtime_feature_option_value_c("doom.presentation","modern-controls",
        "menu-pointer",value,sizeof(value)))menu_pointer_enabled=enabled && !strcmp(value,"on");
    look_enabled = 0;
    jump_enabled = 0;
    crouch_enabled = 0;
    sensitivity = 1;
    invert_y = 0;
    if (snes_mod_runtime_feature_option_value_c("doom.presentation", "modern-controls",
        "sensitivity", value, sizeof(value))) {
        if (!strcmp(value, "0.5")) sensitivity = 0.5;
        else if (!strcmp(value, "1.5")) sensitivity = 1.5;
        else if (!strcmp(value, "2")) sensitivity = 2;
    }
    if (snes_mod_runtime_feature_option_value_c("doom.presentation", "modern-controls",
        "vertical-look", value, sizeof(value))) look_enabled = enabled && !strcmp(value, "on");
    if (snes_mod_runtime_feature_option_value_c("doom.presentation", "modern-controls",
        "jumping", value, sizeof(value))) jump_enabled = enabled && !strcmp(value, "on");
    if (!jump_enabled) jump_queued = 0;
    if (snes_mod_runtime_feature_option_value_c("doom.presentation", "modern-controls",
        "crouching", value, sizeof(value))) crouch_enabled = enabled && !strcmp(value, "on");
    if(!crouch_enabled)crouch_key=crouch_active=0;
    if (snes_mod_runtime_feature_option_value_c("doom.presentation", "modern-controls",
        "invert-y", value, sizeof(value))) invert_y = !strcmp(value, "on");
}
SNES_MOD_CONSTRUCTOR(RegisterDoomControls) {
    snes_mod_register_activation_plugin("doom.controls.modern", ReadSettings);
    snes_mod_register_reset_callback(ReadSettings);
}
static void CaptureMouse(SDL_Window *window) {
    if (window == captured_window) return;
#if SNESRECOMP_SDL3
    if (captured_window) SDL_SetWindowRelativeMouseMode(captured_window, false);
    captured_window = window && SDL_SetWindowRelativeMouseMode(window, true) ? window : NULL;
#else
    SDL_SetRelativeMouseMode(SDL_FALSE);
    captured_window = window && SDL_SetRelativeMouseMode(SDL_TRUE) == 0 ? window : NULL;
#endif
    yaw_counts = 0;
    if (captured_window) blocked_buttons = SDL_GetMouseState(NULL, NULL);
}
static void MenuCursor(int hide) {
    if(hide==menu_cursor_hidden)return;
#if SNESRECOMP_SDL3
    if(hide)SDL_HideCursor();else SDL_ShowCursor();
#else
    SDL_ShowCursor(hide ? SDL_DISABLE : SDL_ENABLE);
#endif
    menu_cursor_hidden=hide;
}
void DoomInputSuspended(int value) {
    suspended = value;
    if(value)MenuCursor(0);
    if(value) { crouch_key=map_wheel=0;menu_pointer_seen=0; }
    if (value) { CaptureMouse(NULL); yaw_counts = 0; queued_weapon = -1;jump_queued=0; menu_motion=0;menu_steps=menu_click=menu_release=0;menu_pad=0;menu_until=0; released = 1; block_keys_next = 1; }
}
void DoomInputReset(void) {
    MenuCursor(0);
    yaw_counts = pitch = 0;
    CaptureMouse(NULL);
    released = suspended ? 1 : 0;
    field = last_movement = 0;
    menu_pointer_seen=0;
    gameplay = 0;
    movement_updates = turns = 0;
    blocked_buttons = 0;
    memset(blocked_keys, 0, sizeof(blocked_keys));
    block_keys_next = 1;
    queued_weapon = -1;weapon_keys = 0;
    menu_motion=0;menu_steps=menu_click=menu_release=0;
    menu_pad=0;menu_until=0;
    jump_queued=jump_active=jump_key=0;jump_z=jump_velocity=0;jump_field=0;
    crouch_key=crouch_active=map_wheel=0;
    weapon_requests=weapon_selections=0;
}
void DoomInputShutdown(void) {
    if (hook_core) RemoveHooks(hook_core);
    hook_core = NULL;
    suspended = 0;
    DoomInputReset();
}
/* _RLP2100 follows dead/falling checks and precedes strafe/turn/forward
 * movement. Apply the native mouse angle scale independently of the pad's
 * horizontal strafe velocity, which retail shares with turning. Private
 * presentation replays never inherit this gameplay hook. */
static void MouseTurn(SuperFx *fx, uint32_t pc, void *context) {
    (void)pc; (void)context;
    last_movement = field;
    gameplay = 1;
    movement_updates++;
    if(enabled && !suspended) {
        /* Retail pjWEAPON is bit 0x40 in the native mover's R9 joystick.
         * Legacy mode leaves this register and original cycling untouched. */
        if(!MapOpen())superfx_set_reg(fx,9,superfx_reg(fx,9)&~0x40u);
        else if(map_wheel) {
            superfx_set_reg(fx,9,(superfx_reg(fx,9)&~0x60u)|(map_wheel>0 ? 0x20u : 0x40u));
            map_wheel+=map_wheel>0 ? -1 : 1;
        }
        menu_motion=0;menu_steps=menu_click=menu_release=0;
        menu_pad=0;
    }
    if (enabled && !suspended && queued_weapon >= 0) {
        static const uint8_t types[7] = {0,2,4,8,10,12,14};
        unsigned type = types[queued_weapon];
        unsigned arms = superfx_ram_peek(fx, kPlayerArms);
        if (!queued_weapon && (arms & 8)) type = 6;
        /* Fists are always available. Native _RLP18000 notices WeaponNext
         * and performs the normal lowering/raising transition. */
        if (!type || (arms & (1u << (type / 2)))) {
            fx->ram[kWeaponNext] = (uint8_t)type;
            fx->ram[kWeaponNext + 1] = 0;
            weapon_selections++;
        }
    }
    queued_weapon = -1;
    unsigned object=(unsigned)(uint16_t)RamSigned(fx,kPlayerPointer);
    if(enabled && !suspended && object>=0x5a8e && object+38<=fx->ram_size) {
        int clearance=RamSigned(fx,object+36)-RamSigned(fx,object+14);
        crouch_active=crouch_key || (crouch_active && clearance<56);
    }
    if(enabled && !suspended && jump_queued && !jump_active &&
       object>=0x5a8e && object+38<=fx->ram_size) {
        int z=RamSigned(fx,object+14),floor=RamSigned(fx,object+34);
        int ceiling=RamSigned(fx,object+36);
        if(!crouch_active && z<=floor && ceiling-z>56) {
            jump_active=1;jump_z=z;jump_velocity=280;jump_field=field-1;
        }
    }
    jump_queued=0;
    double counts = yaw_counts;
    yaw_counts = 0;
    if (!enabled || suspended || !counts) return;
    if (object < 0x5a8e || object + 38 > fx->ram_size) return;
    unsigned address = object + kPlayerAngle;
    uint16_t angle = superfx_ram_peek(fx, address) |
        ((unsigned)superfx_ram_peek(fx, address + 1) << 8);
    angle = DoomMouseAngle(angle, counts);
    turns++;
    fx->ram[address] = (uint8_t)angle;
    fx->ram[address + 1] = (uint8_t)(angle >> 8);
}
static void JumpAirMovement(SuperFx *fx,uint32_t pc,void *context) {
    (void)pc;(void)context;
    if(enabled && !suspended && jump_active)superfx_hook_redirect(fx,kPlayerMovement);
}
static void JumpGravity(SuperFx *fx,uint32_t pc,void *context) {
    (void)pc;(void)context;
    unsigned object=(unsigned)(uint16_t)RamSigned(fx,kPlayerPointer);
    if(!enabled || suspended || !jump_active || superfx_reg(fx,12)!=object ||
       object<0x5a8e || object+38>fx->ram_size)return;
    int floor=(int16_t)superfx_reg(fx,2),z=(int16_t)superfx_reg(fx,1);
    int top=RamSigned(fx,object+36)-(crouch_active ? 28 : 56);
    double dt=fmin(0.1,(field-jump_field)/60.0);jump_field=field;
    jump_z+=jump_velocity*dt-0.5*1225*dt*dt;jump_velocity-=1225*dt;
    if(jump_z>top) { jump_z=top;jump_velocity=fmin(0,jump_velocity); }
    if(jump_z<=floor) { jump_z=floor;jump_active=0; }
    /* Replace only the player's native gravity decrement. The original
     * routine still clamps the floor and writes the real object height. */
    superfx_set_reg(fx,6,(uint16_t)(z-(int)lround(jump_z)));
}
int DoomInputMenuPointer(double *x,double *y) {
    if(!enabled || !menu_pointer_enabled || suspended || !menu_pointer_seen ||
       !hook_core || !DoomNativeMenuActive())return 0;
    if(x)*x=menu_pointer_x;if(y)*y=menu_pointer_y;return 1;
}
static int MenuHover(double x,double y) {
    static const uint8_t signature[]={0x0a,0x0a,0x0a,0x69,0x14,0x00,0xa8,0xb7,0x82};
    if(!hook_core || !g_snes || !g_snes->ram ||
       memcmp(hook_core->rom+0x1fa6e4,signature,sizeof(signature)))return 0;
    uint8_t *ram=g_snes->ram;
    unsigned address=ram[0x82]|ram[0x83]<<8,bank=ram[0x84]&0x7f;
    if(bank<0x40 && address<0x8000)return 0;
    unsigned offset=bank<0x40 ? (bank<<15)|(address&0x7fff) : ((bank-0x40)<<16)|address;
    if(offset+14>hook_core->rom_size)return 0;
    unsigned count=hook_core->rom[offset+13];
    unsigned maximum=ram[0x8a]|ram[0x8b]<<8;
    if(!maximum || maximum>16 || !count || count>16)return 0;
    if(count>maximum)count=maximum;
    if(offset+14+count*8>hook_core->rom_size)return 0;
    for(unsigned i=0;i<count;i++) {
        const uint8_t *xy=hook_core->rom+offset+14+i*8+6;
        if(x>=xy[0]+12 && x<240 && fabs(y-(xy[1]+8))<=8) {
            ram[0x86]=(uint8_t)i;ram[0x87]=0;return 1;
        }
    }
    return 0;
}
int DoomInputMenuPosition(double x,double y,int click) {
    if(!enabled || !menu_pointer_enabled || suspended || !isfinite(x) || !isfinite(y) || !DoomNativeMenuActive())return 0;
    menu_pointer_x=x;menu_pointer_y=y;menu_pointer_seen=1;
    int hit=MenuHover(x,y);
    if(click && hit)menu_click=1;
    return 1;
}
static void MenuWindowPosition(double x,double y,int click) {
    int w=0,h=0;double nx,ny;
    SDL_GetWindowSize(SDL_GetKeyboardFocus(),&w,&h);
    if(DoomPresentationMapPointer(x,y,w,h,&nx,&ny))DoomInputMenuPosition(nx,ny,click);
    else menu_pointer_seen=0;
}
void DoomInputBeforeFrame(void) {
    ReadSettings();
    ++field;
    if(DoomNativeMenuActive())CaptureMouse(NULL);
    if(!gameplay || suspended)jump_field=field;
    if(hook_core && RamSigned(hook_core,0x2c)!=0)jump_active=jump_queued=0;
    /* The native pause/menu path stops calling the player mover. Discard
     * motion while it is stopped so resuming cannot unleash a queued turn. */
    if (gameplay && field - last_movement > 12) {
        gameplay = 0;
        queued_weapon = -1;
        jump_queued=0;
        CaptureMouse(NULL);
        yaw_counts = 0;
    }
    SuperFx *fx = g_snes && g_snes->cart ? g_snes->cart->superfx : NULL;
    if (fx != hook_core) { hook_core = NULL;crouch_hooks=0; }
    static const uint8_t entry[] = {0x29,0x10,0xa1,0x20,0x71,0x09,0x07,0x01};
    static const uint8_t angle_load[] = {0xa0,0x14,0x3d,0xa1,0xc6,0x51,0x40,0x56,0x3f,0x71,0x90};
    static const uint8_t fall_check[]={0xa0,0x22,0x3d,0xa2,0xc6,0x52,0x11,0x40};
    static const uint8_t gravity[]={0xb1,0x66,0x3f,0x62,0x06,0x02,0x20,0xb2,0x90,0x9b,0x01};
    static const uint8_t eye[]={0xa1,0x2a,0x56,0x3f,0x61,0x07,0x02,0x01,0xb1,0x90};
    static const uint8_t clearance[]={0xa6,0x38,0x21,0x56,0x3f,0x61,0x07,0x08};
    static const uint8_t ceiling_object[]={0xef,0x51,0x3f,0x63,0x07,0xdc};
    static const uint8_t ceiling_player[]={0xa6,0x38,0x21,0x56,0x61,0x07,0xaa};
    int supported = fx && fx->rom_size == 0x200000 && fx->ram_size >= 0x10000 &&
        !memcmp(fx->rom + 0x1c1, entry, sizeof(entry)) &&
        !memcmp(fx->rom + 0x23b, angle_load, sizeof(angle_load)) &&
        !memcmp(fx->rom+0x1ac,fall_check,sizeof(fall_check)) &&
        !memcmp(fx->rom+0x12c8,gravity,sizeof(gravity)) &&
        !memcmp(fx->rom+0x38a,eye,sizeof(eye)) &&
        !memcmp(fx->rom+0x2521,clearance,sizeof(clearance)) &&
        !memcmp(fx->rom+0x5e4e,ceiling_object,sizeof(ceiling_object)) &&
        !memcmp(fx->rom+0x5e7f,ceiling_player,sizeof(ceiling_player));
    if (enabled && supported) {
        /* Ordinary savestate loads reset the host adapter. Recover a crouch
         * from the native eye/clearance before allowing it to stand. */
        unsigned player=(unsigned)(uint16_t)RamSigned(fx,kPlayerPointer);
        if(crouch_enabled && player>=0x5a8e && player+38<=fx->ram_size && !RamSigned(fx,0x2c) &&
           RamSigned(fx,kPlayerHeight)>0 && RamSigned(fx,kPlayerHeight)<=21 &&
           RamSigned(fx,player+36)-RamSigned(fx,player+14)<56)crouch_active=1;
        if (!hook_core) {
            if(superfx_set_pc_hook(fx,kPlayerMovement,MouseTurn,NULL) &&
               superfx_set_pc_hook(fx,kPlayerFallCheck,JumpAirMovement,NULL) &&
               superfx_set_pc_hook(fx,kObjectGravity,JumpGravity,NULL))hook_core=fx;
            else RemoveHooks(fx);
        }
        /* Crouch adds four instruction observers. Install them only while
         * needed so ordinary modern controls retain their original cost. */
        int need_crouch=crouch_enabled && !suspended && (crouch_key || crouch_active);
        if(hook_core && need_crouch!=crouch_hooks) {
            if(superfx_set_pc_hook(fx,kEyeClamp,need_crouch ? CrouchEye : NULL,NULL) &&
               superfx_set_pc_hook(fx,kMoveClearance,need_crouch ? CrouchClearance : NULL,NULL) &&
               superfx_set_pc_hook(fx,kCeilingObjectHeight,need_crouch ? CrouchCeiling : NULL,NULL) &&
               superfx_set_pc_hook(fx,kCeilingPlayerHeight,need_crouch ? CrouchCeiling : NULL,NULL))
                crouch_hooks=need_crouch;
            else { RemoveHooks(fx);hook_core=NULL; }
        }
    } else {
        if (hook_core) RemoveHooks(hook_core);
        hook_core = NULL;
        CaptureMouse(NULL);
        yaw_counts = pitch = 0;
        queued_weapon = -1;
        jump_queued=jump_active=0;
        crouch_key=crouch_active=map_wheel=0;
        menu_motion=0;menu_steps=menu_click=menu_release=0;menu_pad=0;menu_until=0;
    }
    if(enabled && menu_pointer_enabled && !suspended && DoomNativeMenuActive()) {
        SDL_Window *focus=SDL_GetKeyboardFocus();
        if(focus && !menu_pointer_seen) {
#if SNESRECOMP_SDL3
            float x=0,y=0;
#else
            int x=0,y=0;
#endif
            SDL_GetMouseState(&x,&y);MenuWindowPosition(x,y,0);
        }
        MenuCursor(focus && menu_pointer_seen);
    } else { menu_pointer_seen=0;MenuCursor(0); }
    if (!look_enabled) pitch = 0;
}
int DoomInputLookEnabled(void) { return look_enabled && hook_core != NULL; }
double DoomInputPitch(void) { return DoomInputLookEnabled() ? pitch : 0; }
void DoomInputGetStats(DoomInputStats *out) {
    *out = (DoomInputStats){movement_updates, turns, enabled,
        gameplay, captured_window != NULL, DoomInputPitch(),weapon_requests,weapon_selections,jump_active,suspended};
}
int DoomInputMotion(double x, double y) {
    if (!enabled || !gameplay || suspended || !isfinite(x) || !isfinite(y)) return 0;
    yaw_counts = fmax(-256, fmin(256, yaw_counts + x * sensitivity));
    if (look_enabled) pitch = DoomMousePitch(pitch, y * sensitivity, invert_y);
    return 1;
}
int DoomInputRequestWeapon(unsigned slot) {
    if(!enabled || !gameplay || suspended || slot>=7)return 0;
    queued_weapon=(int)slot;weapon_requests++;return 1;
}
int DoomInputRequestJump(void) {
    if(!enabled || !jump_enabled || !gameplay || suspended)return 0;
    jump_queued=1;return 1;
}
int DoomInputRequestCrouch(int held) {
    if(!enabled || !crouch_enabled || !gameplay || suspended || MapOpen())return 0;
    crouch_key=held!=0;return 1;
}
int DoomInputRequestMapZoom(int steps) {
    if(!enabled || !gameplay || suspended || !MapOpen())return 0;
    map_wheel=(int)fmax(-16,fmin(16,map_wheel+steps*3));return 1;
}

uint16_t DoomInputKeyboard(const uint8_t *keys, unsigned player, uint16_t defaults) {
    ReadSettings();
    if (!enabled || player) return defaults;
    SDL_Window *focus = SDL_GetKeyboardFocus();
    if (!focus || suspended) { CaptureMouse(NULL); queued_weapon = -1;crouch_key=0;map_wheel=0; return 0; }
    if (!released && gameplay && !DoomNativeMenuActive()) CaptureMouse(focus);
    if (block_keys_next) {
        memcpy(blocked_keys, keys, sizeof(blocked_keys));
        block_keys_next = 0;
    }
    uint8_t filtered_keys[SDL_SCANCODE_COUNT];
    for (unsigned i = 0; i < SDL_SCANCODE_COUNT; i++) {
        blocked_keys[i] &= keys[i];
        filtered_keys[i] = keys[i] && !blocked_keys[i];
    }
    keys = filtered_keys;
    /* Ctrl alone crouches; modified host shortcuts still retain ownership. */
    crouch_key=crouch_enabled && gameplay && !MapOpen() &&
        (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) &&
        !(keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT] ||
          keys[SDL_SCANCODE_L] || keys[SDL_SCANCODE_R]);
    unsigned pressed = 0;
    int shortcut = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL] ||
                   keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];
    unsigned numbers = 0;
    for (unsigned slot = 0; slot < 7; slot++)
        if (keys[SDL_SCANCODE_1 + slot]) numbers |= 1u << slot;
    if (!shortcut && gameplay)
        for (unsigned slot = 0; slot < 7; slot++)
            if ((numbers & ~weapon_keys) & (1u << slot)) DoomInputRequestWeapon(slot);
    weapon_keys = numbers;
    if(!shortcut && gameplay && keys[SDL_SCANCODE_SPACE] && !jump_key)DoomInputRequestJump();
    jump_key=keys[SDL_SCANCODE_SPACE];
    if (!shortcut || crouch_key) {
        if (keys[SDL_SCANCODE_W]) pressed |= DOOM_KEY_FORWARD;
        if (keys[SDL_SCANCODE_S]) pressed |= DOOM_KEY_BACK;
        if (keys[SDL_SCANCODE_A]) pressed |= DOOM_KEY_LEFT;
        if (keys[SDL_SCANCODE_D]) pressed |= DOOM_KEY_RIGHT;
        if (!crouch_key && (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])) pressed |= DOOM_KEY_RUN;
        if (keys[SDL_SCANCODE_E]) pressed |= DOOM_KEY_USE;
        if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER]) pressed |= DOOM_KEY_PAUSE;
        if (keys[SDL_SCANCODE_TAB]) pressed |= DOOM_KEY_MAP;
        if (keys[SDL_SCANCODE_HOME]) pitch = 0;
    }
    uint32_t mouse = SDL_GetMouseState(NULL, NULL);
    blocked_buttons &= mouse;
    mouse &= ~blocked_buttons;
    if (captured_window) {
        if (mouse & SDL_BUTTON_LMASK) pressed |= DOOM_KEY_FIRE;
        if (mouse & SDL_BUTTON_RMASK) pressed |= DOOM_KEY_USE;
    }
    uint16_t pad = DoomKeyboardPad(pressed);
    if(MapOpen() && !shortcut) {
        int zoom_in=keys[SDL_SCANCODE_EQUALS] || keys[SDL_SCANCODE_KP_PLUS];
        int zoom_out=keys[SDL_SCANCODE_MINUS] || keys[SDL_SCANCODE_KP_MINUS];
        pad &= ~((1u<<1)|(1u<<9));
        if(zoom_in && !zoom_out)pad |= 1u<<1;
        if(zoom_out && !zoom_in)pad |= 1u<<9;
    }
    if (keys[SDL_SCANCODE_UP]) pad |= 1u << 4;
    if (keys[SDL_SCANCODE_DOWN]) pad |= 1u << 5;
    if (keys[SDL_SCANCODE_LEFT]) pad |= 1u << 6;
    if (keys[SDL_SCANCODE_RIGHT]) pad |= 1u << 7;
    return pad;
}
uint16_t DoomInputAuxiliary(unsigned player,uint16_t pad) {
    if(player || !enabled || suspended || !SDL_GetKeyboardFocus())return pad;
    if(gameplay && !DoomNativeMenuActive()) {
        if(!MapOpen()) { map_wheel=0;return pad & ~(1u<<9); }
        if(map_wheel) {
            pad &= ~((1u<<1)|(1u<<9));
            pad |= map_wheel>0 ? 1u<<1 : 1u<<9;
        }
        return pad;
    }
    /* Hold/release for actual simulated fields, not presentation polls;
     * otherwise a high host refresh rate can lose native menu key edges. */
    if(menu_pad) {
        if(field<menu_until)return pad|menu_pad;
        menu_pad=0;menu_release=1;menu_until=field+2;return pad;
    }
    if(menu_release) {
        if(field<menu_until)return pad;
        menu_release=0;
    }
    if(menu_click) { menu_pad=1u<<8;menu_click=0; }
    else if(menu_steps) {
        menu_pad=menu_steps<0 ? 1u<<4 : 1u<<5;
        menu_steps+=menu_steps<0 ? 1 : -1;
    }
    if(menu_pad)menu_until=field+2;
    return pad|menu_pad;
}
int DoomInputEvent(const void *opaque) {
    const SDL_Event *event = opaque;
    /* Cheats see this event first, so the P in IDCLIP still works. Doom's
     * Start button owns pausing; never pass P to the host pause shortcut. */
    if (event && (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) &&
        SNESRECOMP_SDL_EVENT_KEY(*event) == SDLK_p) return 1;
    /* R occurs in IDCHOPPERS and powerup cheats. The host's plain-R
     * diagnostic switches to a PPU without BG/OBJ overlay extraction,
     * erasing the enhanced pause menu and making scanout much slower.
     * Keep modified shortcuts such as Ctrl+R (reset) available. */
    if (event && (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) &&
        SNESRECOMP_SDL_EVENT_KEY(*event) == SDLK_r &&
        !(SNESRECOMP_SDL_EVENT_MOD(*event) & (KMOD_CTRL|KMOD_ALT|KMOD_GUI))) return 1;
    if (!enabled) return 0;
    if(event->type==SDL_MOUSEWHEEL && MapOpen() && !suspended && SDL_GetKeyboardFocus()) {
        int steps=event->wheel.y;
        if(event->wheel.direction==SDL_MOUSEWHEEL_FLIPPED)steps=-steps;
        DoomInputRequestMapZoom(steps);
        return 1;
    }
#if SNESRECOMP_SDL3
    int lost_focus = event->type == SDL_EVENT_WINDOW_FOCUS_LOST;
#else
    int lost_focus = event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_FOCUS_LOST;
#endif
    if (lost_focus) { MenuCursor(0);menu_pointer_seen=0; CaptureMouse(NULL); yaw_counts = 0; queued_weapon = -1;jump_queued=0;crouch_key=map_wheel=0; menu_motion=0;menu_steps=menu_click=menu_release=0;menu_pad=0;menu_until=0; released = 1; block_keys_next = 1; return 0; }
    if(event->type==SDL_MOUSEMOTION && (!gameplay || DoomNativeMenuActive()) && !suspended && SDL_GetKeyboardFocus()) {
        if(menu_pointer_enabled && DoomNativeMenuActive()) {
            CaptureMouse(NULL);MenuWindowPosition(event->motion.x,event->motion.y,0);return 1;
        }
        menu_motion+=event->motion.yrel;
        int steps=(int)(menu_motion/24);
        menu_motion-=steps*24;
        menu_steps=(int)fmax(-8,fmin(8,menu_steps+steps));
        return 1;
    }
    if(event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT &&
       (!gameplay || DoomNativeMenuActive()) && !suspended && SDL_GetKeyboardFocus()) {
        if(menu_pointer_enabled && DoomNativeMenuActive())MenuWindowPosition(event->button.x,event->button.y,1);
        else menu_click=1;return 1; }
    if (event->type == SDL_MOUSEMOTION && captured_window && !suspended) {
        DoomInputMotion(event->motion.xrel, event->motion.yrel);
        return 1;
    }
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT && !suspended && gameplay) {
        if (!captured_window) {
            CaptureMouse(SDL_GetKeyboardFocus()); released = 0;
            blocked_buttons |= SDL_BUTTON_LMASK;
        }
        return 1;
    }
    if (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) return captured_window != NULL;
    if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
        int key = SNESRECOMP_SDL_EVENT_KEY(*event);
        int mod = SNESRECOMP_SDL_EVENT_MOD(*event);
        if(MapOpen() && !suspended && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI)) &&
           (key==SDLK_PLUS || key==SDLK_EQUALS || key==SDLK_MINUS ||
            key==SDLK_KP_PLUS || key==SDLK_KP_MINUS))return 1;
        if(event->type==SDL_KEYDOWN && !event->key.repeat && gameplay && !suspended &&
           !(mod&(KMOD_CTRL|KMOD_ALT)) && key>=SDLK_1 && key<=SDLK_7)
            DoomInputRequestWeapon((unsigned)(key-SDLK_1));
        if(event->type==SDL_KEYDOWN && !event->key.repeat && key==SDLK_SPACE &&
           gameplay && !suspended && !(mod&(KMOD_CTRL|KMOD_ALT)))DoomInputRequestJump();
        if (event->type == SDL_KEYDOWN && (key == SDLK_RETURN || key == SDLK_KP_ENTER) &&
            !(mod & (KMOD_CTRL|KMOD_ALT))) {
            CaptureMouse(NULL); yaw_counts = 0; released = 1;
            queued_weapon = -1;
            jump_queued = 0;
        }
        if (key == SDLK_ESCAPE && captured_window) {
            CaptureMouse(NULL); released = 1; return 1;
        }
        if (!(mod & (KMOD_CTRL|KMOD_ALT)) &&
            (key == SDLK_w || key == SDLK_s || key == SDLK_a || key == SDLK_d ||
             key == SDLK_e || key == SDLK_RETURN || key == SDLK_KP_ENTER ||
             key == SDLK_LSHIFT || key == SDLK_RSHIFT || key == SDLK_SPACE ||
             key == SDLK_TAB || key == SDLK_HOME || (key >= SDLK_1 && key <= SDLK_7))) return 1;
    }
    return 0;
}

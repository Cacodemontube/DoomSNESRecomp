#include "doom_input.h"
#include "doom_input_math.h"
#include "common_cpu_infra.h"
#include "mod_runtime.h"
#include "snes/cart.h"
#include "snes/superfx.h"
#define SDL_MAIN_HANDLED 1
#include "desktop/sdl_compat.h"
#include <string.h>

static int enabled, look_enabled, invert_y, suspended, released;
static double sensitivity = 1, yaw_counts, pitch;
static SDL_Window *captured_window;
static SuperFx *hook_core;
static unsigned field, last_movement;
static int gameplay;
static unsigned movement_updates, turns;
static uint32_t blocked_buttons;
static uint8_t blocked_keys[SDL_SCANCODE_COUNT];
static int block_keys_next;
enum { kPlayerMovement = 0x81c1, kPlayerPointer = 0x18c, kPlayerAngle = 20 };

static void ReadSettings(void) {
    char value[32];
    enabled = snes_mod_runtime_feature_enabled_c("doom.presentation", "modern-controls");
    look_enabled = enabled;
    sensitivity = 1;
    invert_y = 0;
    if (snes_mod_runtime_feature_option_value_c("doom.presentation", "modern-controls",
        "sensitivity", value, sizeof(value))) {
        if (!strcmp(value, "0.5")) sensitivity = 0.5;
        else if (!strcmp(value, "1.5")) sensitivity = 1.5;
        else if (!strcmp(value, "2")) sensitivity = 2;
    }
    if (snes_mod_runtime_feature_option_value_c("doom.presentation", "modern-controls",
        "vertical-look", value, sizeof(value))) look_enabled = enabled && strcmp(value, "off");
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
void DoomInputSuspended(int value) {
    suspended = value;
    if (value) { CaptureMouse(NULL); yaw_counts = 0; released = 1; block_keys_next = 1; }
}
void DoomInputReset(void) {
    yaw_counts = pitch = 0;
    CaptureMouse(NULL);
    released = suspended ? 1 : 0;
    field = last_movement = 0;
    gameplay = 0;
    movement_updates = turns = 0;
    blocked_buttons = 0;
    memset(blocked_keys, 0, sizeof(blocked_keys));
    block_keys_next = 1;
}
void DoomInputShutdown(void) {
    if (hook_core) superfx_set_pc_hook(hook_core, kPlayerMovement, NULL, NULL);
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
    double counts = yaw_counts;
    yaw_counts = 0;
    if (!enabled || suspended || !counts) return;
    unsigned object = superfx_ram_peek(fx, kPlayerPointer) |
        ((unsigned)superfx_ram_peek(fx, kPlayerPointer + 1) << 8);
    if (object < 0x5a8e || object + 38 > fx->ram_size) return;
    unsigned address = object + kPlayerAngle;
    uint16_t angle = superfx_ram_peek(fx, address) |
        ((unsigned)superfx_ram_peek(fx, address + 1) << 8);
    angle = DoomMouseAngle(angle, counts);
    turns++;
    fx->ram[address] = (uint8_t)angle;
    fx->ram[address + 1] = (uint8_t)(angle >> 8);
}
void DoomInputBeforeFrame(void) {
    ReadSettings();
    ++field;
    /* The native pause/menu path stops calling the player mover. Discard
     * motion while it is stopped so resuming cannot unleash a queued turn. */
    if (gameplay && field - last_movement > 12) {
        gameplay = 0;
        CaptureMouse(NULL);
        yaw_counts = 0;
    }
    SuperFx *fx = g_snes && g_snes->cart ? g_snes->cart->superfx : NULL;
    if (fx != hook_core) hook_core = NULL;
    static const uint8_t entry[] = {0x29,0x10,0xa1,0x20,0x71,0x09,0x07,0x01};
    static const uint8_t angle_load[] = {0xa0,0x14,0x3d,0xa1,0xc6,0x51,0x40,0x56,0x3f,0x71,0x90};
    int supported = fx && fx->rom_size == 0x200000 && fx->ram_size >= 0x10000 &&
        !memcmp(fx->rom + 0x1c1, entry, sizeof(entry)) &&
        !memcmp(fx->rom + 0x23b, angle_load, sizeof(angle_load));
    if (enabled && supported) {
        if (!hook_core && superfx_set_pc_hook(fx, kPlayerMovement, MouseTurn, NULL)) hook_core = fx;
    } else {
        if (hook_core) superfx_set_pc_hook(hook_core, kPlayerMovement, NULL, NULL);
        hook_core = NULL;
        CaptureMouse(NULL);
        yaw_counts = pitch = 0;
    }
    if (!look_enabled) pitch = 0;
}
int DoomInputLookEnabled(void) { return look_enabled && hook_core != NULL; }
double DoomInputPitch(void) { return DoomInputLookEnabled() ? pitch : 0; }
void DoomInputGetStats(DoomInputStats *out) {
    *out = (DoomInputStats){movement_updates, turns, enabled,
        gameplay, captured_window != NULL, DoomInputPitch()};
}
int DoomInputMotion(double x, double y) {
    if (!enabled || !gameplay || suspended || !isfinite(x) || !isfinite(y)) return 0;
    yaw_counts = fmax(-256, fmin(256, yaw_counts + x * sensitivity));
    if (look_enabled) pitch = DoomMousePitch(pitch, y * sensitivity, invert_y);
    return 1;
}

uint16_t DoomInputKeyboard(const uint8_t *keys, unsigned player, uint16_t defaults) {
    ReadSettings();
    if (!enabled || player) return defaults;
    SDL_Window *focus = SDL_GetKeyboardFocus();
    if (!focus || suspended) { CaptureMouse(NULL); return 0; }
    if (!released && gameplay) CaptureMouse(focus);
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
    unsigned pressed = 0;
    int shortcut = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL] ||
                   keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];
    if (!shortcut) {
        if (keys[SDL_SCANCODE_W]) pressed |= DOOM_KEY_FORWARD;
        if (keys[SDL_SCANCODE_S]) pressed |= DOOM_KEY_BACK;
        if (keys[SDL_SCANCODE_A]) pressed |= DOOM_KEY_LEFT;
        if (keys[SDL_SCANCODE_D]) pressed |= DOOM_KEY_RIGHT;
        if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) pressed |= DOOM_KEY_RUN;
        if (keys[SDL_SCANCODE_E]) pressed |= DOOM_KEY_USE;
        if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER]) pressed |= DOOM_KEY_PAUSE;
        if (keys[SDL_SCANCODE_SPACE]) pressed |= DOOM_KEY_WEAPON;
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
    if (keys[SDL_SCANCODE_UP]) pad |= 1u << 4;
    if (keys[SDL_SCANCODE_DOWN]) pad |= 1u << 5;
    if (keys[SDL_SCANCODE_LEFT]) pad |= 1u << 6;
    if (keys[SDL_SCANCODE_RIGHT]) pad |= 1u << 7;
    return pad;
}
int DoomInputEvent(const void *opaque) {
    const SDL_Event *event = opaque;
    ReadSettings();
    if (!enabled) return 0;
#if SNESRECOMP_SDL3
    int lost_focus = event->type == SDL_EVENT_WINDOW_FOCUS_LOST;
#else
    int lost_focus = event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_FOCUS_LOST;
#endif
    if (lost_focus) { CaptureMouse(NULL); yaw_counts = 0; released = 1; block_keys_next = 1; return 0; }
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
        if (event->type == SDL_KEYDOWN && (key == SDLK_RETURN || key == SDLK_KP_ENTER) &&
            !(mod & (KMOD_CTRL|KMOD_ALT))) {
            CaptureMouse(NULL); yaw_counts = 0; released = 1;
        }
        if (key == SDLK_ESCAPE && captured_window) {
            CaptureMouse(NULL); released = 1; return 1;
        }
        if (!(mod & (KMOD_CTRL|KMOD_ALT)) &&
            (key == SDLK_w || key == SDLK_s || key == SDLK_a || key == SDLK_d ||
             key == SDLK_e || key == SDLK_RETURN || key == SDLK_KP_ENTER ||
             key == SDLK_LSHIFT || key == SDLK_RSHIFT || key == SDLK_SPACE ||
             key == SDLK_TAB || key == SDLK_HOME)) return 1;
    }
    return 0;
}

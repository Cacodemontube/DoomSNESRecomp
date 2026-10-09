#include "doom_debug.h"
#if SNESRECOMP_TRACE
#include "doom_renderer.h"
#include "doom_input.h"
#include "common_cpu_infra.h"
#include "snes/snes.h"
#include "snes/cart.h"
#include "snes/superfx.h"
#include "desktop/sdl_compat.h"
#include <stdio.h>
#include <string.h>

/* The TCP thread only reads a published snapshot. It never inspects the
 * live renderer or its workers while the host is modifying them. */
static SDL_SpinLock lock;
static DoomRendererStats stats;
static unsigned frame;
static unsigned p1, p2, invisibility_ticks;
static int requested_invisibility_ticks = -1;
static int requested_motion, motion_x, motion_y;
static int requested_key;
static DoomInputStats input_stats;
static unsigned long long draws;
static double draw_total_ms, draw_max_ms;
#if SNESRECOMP_SDL3
#define LOCK() SDL_LockSpinlock(&lock)
#define UNLOCK() SDL_UnlockSpinlock(&lock)
#else
#define LOCK() SDL_AtomicLock(&lock)
#define UNLOCK() SDL_AtomicUnlock(&lock)
#endif

void DoomDebugRecordFrame(unsigned number) {
    /* Test requests are applied by the emulation thread at a completed field,
     * never by the TCP thread while CPU/GSU execution or replay is active. */
    LOCK();
    int request = requested_invisibility_ticks;
    requested_invisibility_ticks = -1;
    int mouse_request = requested_motion, dx = motion_x, dy = motion_y;
    requested_motion = 0;
    int key_request=requested_key;requested_key=0;
    UNLOCK();
    if (mouse_request) DoomInputMotion(dx, dy);
    if(key_request) {
        if(key_request==SDLK_SPACE)DoomInputRequestJump();
        else DoomInputRequestWeapon((unsigned)(key_request-SDLK_1));
    }
    SuperFx *fx = g_snes && g_snes->cart ? g_snes->cart->superfx : NULL;
    if (request >= 0 && fx && fx->ram && fx->ram_size > 0x1eb &&
        g_snes->ram && (g_snes->ram[0x2c] & 0x40)) {
        fx->ram[0x1ea] = (uint8_t)request;
        fx->ram[0x1eb] = (uint8_t)(request >> 8);
    }
    unsigned ticks = fx && fx->ram && fx->ram_size > 0x1eb
        ? fx->ram[0x1ea] | ((unsigned)fx->ram[0x1eb] << 8) : 0;
    DoomRendererStats snapshot;
    DoomRendererGetStats(&snapshot);
    DoomInputStats input_snapshot;
    DoomInputGetStats(&input_snapshot);
    LOCK();
    stats = snapshot;
    input_stats = input_snapshot;
    frame = number;
    invisibility_ticks = ticks;
    p1 = g_snes ? g_snes->input1_currentState : 0;
    p2 = g_snes ? g_snes->input2_currentState : 0;
    UNLOCK();
}

void DoomDebugRecordDraw(double milliseconds) {
    DoomRendererStats snapshot;
    DoomRendererGetStats(&snapshot);
    LOCK();
    stats = snapshot;
    draws++;
    draw_total_ms += milliseconds;
    if (milliseconds > draw_max_ms) draw_max_ms = milliseconds;
    UNLOCK();
}

static int Command(const char *cmd, const char *args, DebugServerGameSendLine send) {
    if(!strcmp(cmd,"modern_key_test")) {
        int key=!strcmp(args,"space") ? SDLK_SPACE :
            strlen(args)==1 && args[0]>='1' && args[0]<='7' ? args[0] : 0;
        LOCK();int ready=input_stats.enabled && input_stats.gameplay;
        if(key && ready)requested_key=key;UNLOCK();
        send(key && ready ? "{\"ok\":true,\"queued\":true}" :
            "{\"ok\":false,\"error\":\"usage during modern gameplay: modern_key_test <space|1..7>\"}");
        return 1;
    }
    if (!strcmp(cmd, "mouse_motion_test")) {
        int dx, dy; char extra;
        if (sscanf(args, "%d %d %c", &dx, &dy, &extra) != 2 ||
            dx < -256 || dx > 256 || dy < -256 || dy > 256) {
            send("{\"ok\":false,\"error\":\"usage: mouse_motion_test <x -256..256> <y -256..256>\"}");
            return 1;
        }
        LOCK();
        int ready = input_stats.enabled && input_stats.gameplay;
        if (ready) { motion_x = dx; motion_y = dy; requested_motion = 1; }
        UNLOCK();
        send(ready ? "{\"ok\":true,\"queued\":true}" :
            "{\"ok\":false,\"error\":\"modern controls must be enabled during gameplay\"}");
        return 1;
    }
    if (!strcmp(cmd, "input_stats")) {
        char json[512];
        LOCK();
        snprintf(json, sizeof(json),
            "{\"ok\":true,\"frame\":%u,\"enabled\":%s,\"gameplay\":%s,\"captured\":%s,"
            "\"movement_updates\":%u,\"mouse_turns\":%u,\"horizon_offset\":%.3f,"
            "\"weapon_requests\":%u,\"weapon_selections\":%u,\"jumping\":%s,\"suspended\":%s}",
            frame, input_stats.enabled ? "true" : "false", input_stats.gameplay ? "true" : "false",
            input_stats.captured ? "true" : "false", input_stats.movement_updates,
            input_stats.turns, input_stats.pitch,input_stats.weapon_requests,input_stats.weapon_selections,
            input_stats.jumping ? "true" : "false",input_stats.suspended ? "true" : "false");
        UNLOCK(); send(json); return 1;
    }
    if (!strcmp(cmd, "weapon_invisibility_test")) {
        unsigned ticks;
        char extra;
        if (sscanf(args, "%u %c", &ticks, &extra) != 1 || ticks > 1800) {
            send("{\"ok\":false,\"error\":\"usage: weapon_invisibility_test <ticks 0..1800>\"}");
            return 1;
        }
        LOCK();
        bool ready = stats.supported && stats.has_snapshot;
        if (ready) requested_invisibility_ticks = (int)ticks;
        UNLOCK();
        send(ready ? "{\"ok\":true,\"queued\":true}" :
                     "{\"ok\":false,\"error\":\"supported gameplay must be running\"}");
        return 1;
    }
    if (strcmp(cmd, "presentation_stats")) return 0;
    if (*args) {
        send("{\"ok\":false,\"error\":\"presentation_stats takes no arguments\"}");
        return 1;
    }
    char json[1024];
    LOCK();
    snprintf(json, sizeof(json),
        "{\"ok\":true,\"frame\":%u,\"p1_input\":%u,\"p2_input\":%u,\"supported\":%s,\"has_snapshot\":%s,"
        "\"captures\":%llu,\"camera_passes\":%llu,\"cache_hits\":%llu,"
        "\"failures\":%llu,\"replay_instructions\":%llu,\"snapshot_interval_fields\":%u,"
        "\"weapon_updates\":%llu,\"weapon_interpolated_presentations\":%llu,"
        "\"weapon_offset_x\":%d,\"weapon_offset_y\":%d,"
        "\"weapon_tiles\":%u,\"weapon_layout_fallbacks\":%llu,\"weapon_translucent\":%s,"
        "\"invisibility_ticks\":%u,\"resolution_scale\":%u,\"resolution_segments\":%u,"
        "\"draw_calls\":%llu,\"draw_total_ms\":%.6f,\"draw_max_ms\":%.6f}",
        frame, p1, p2, stats.supported ? "true" : "false", stats.has_snapshot ? "true" : "false",
        (unsigned long long)stats.captures, (unsigned long long)stats.camera_passes,
        (unsigned long long)stats.cache_hits, (unsigned long long)stats.failures,
        (unsigned long long)stats.replay_instructions, stats.snapshot_interval,
        (unsigned long long)stats.weapon_updates,
        (unsigned long long)stats.weapon_interpolated_presentations,
        stats.weapon_offset_x, stats.weapon_offset_y,
        stats.weapon_tiles, (unsigned long long)stats.weapon_layout_fallbacks,
        stats.weapon_translucent ? "true" : "false", invisibility_ticks,
        stats.resolution_scale, stats.resolution_segments,
        draws, draw_total_ms, draw_max_ms);
    UNLOCK();
    send(json);
    return 1;
}

void DoomDebugInit(void) { debug_server_set_game_command_handler(Command); }
#endif

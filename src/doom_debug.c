#include "doom_debug.h"
#if SNESRECOMP_TRACE
#include "doom_renderer.h"
#include "doom_input.h"
#include "doom_cheats.h"
#include "doom_music.h"
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
static int requested_pointer,pointer_x,pointer_y,pointer_click;
static DoomInputStats input_stats;
static DoomCheatsStats cheat_stats;
static char requested_cheat[11];
static int requested_cheat_keyboard;
static int requested_level_exit;
static int music_loading;
static unsigned long long draws;
static double draw_total_ms, draw_max_ms;
#if SNESRECOMP_SDL3
#define LOCK() SDL_LockSpinlock(&lock)
#define UNLOCK() SDL_UnlockSpinlock(&lock)
#else
#define LOCK() SDL_AtomicLock(&lock)
#define UNLOCK() SDL_AtomicUnlock(&lock)
#endif

void DoomDebugApplyRequests(void) {
    /* Test requests are applied by the emulation thread at a completed field,
     * never by the TCP thread while CPU/GSU execution or replay is active. */
    LOCK();
    int request = requested_invisibility_ticks;
    requested_invisibility_ticks = -1;
    int mouse_request = requested_motion, dx = motion_x, dy = motion_y;
    requested_motion = 0;
    int pointer_request=requested_pointer,px=pointer_x,py=pointer_y,click=pointer_click;
    requested_pointer=0;
    int key_request=requested_key;requested_key=0;
    char cheat_request[sizeof(requested_cheat)];
    memcpy(cheat_request,requested_cheat,sizeof(cheat_request));requested_cheat[0]=0;
    int cheat_keyboard=requested_cheat_keyboard;requested_cheat_keyboard=0;
    int level_exit=requested_level_exit;requested_level_exit=0;
    UNLOCK();
    if(cheat_request[0] && cheat_keyboard) {
        /* Exercise the same event routing as typing, including host hotkeys. */
        for(const char *p=cheat_request;*p;p++) {
            SDL_Event event;memset(&event,0,sizeof(event));
            int key=*p>='A' && *p<='Z'?*p+'a'-'A':*p;
            event.type=SDL_KEYDOWN;
#if SNESRECOMP_SDL3
            event.key.key=key;
#else
            event.key.keysym.sym=key;
#endif
            SDL_PushEvent(&event);
            event.type=SDL_KEYUP;SDL_PushEvent(&event);
        }
    } else if(cheat_request[0])DoomCheatsTestText(cheat_request);
    if(level_exit)DoomCheatsTestExit();
    if (mouse_request) DoomInputMotion(dx, dy);
    if(pointer_request)DoomInputMenuPosition(px,py,click);
    if(key_request) {
        if(key_request==SDLK_SPACE)DoomInputRequestJump();
        else if(key_request==SDLK_LCTRL)DoomInputRequestCrouch(1);
        else if(key_request==SDLK_RCTRL)DoomInputRequestCrouch(0);
        else if(key_request==SDLK_PLUS)DoomInputRequestMapZoom(1);
        else if(key_request==SDLK_MINUS)DoomInputRequestMapZoom(-1);
        else DoomInputRequestWeapon((unsigned)(key_request-SDLK_1));
    }
    SuperFx *fx = g_snes && g_snes->cart ? g_snes->cart->superfx : NULL;
    if (request >= 0 && fx && fx->ram && fx->ram_size > 0x1eb &&
        g_snes->ram && (g_snes->ram[0x2c] & 0x40)) {
        fx->ram[0x1ea] = (uint8_t)request;
        fx->ram[0x1eb] = (uint8_t)(request >> 8);
    }
}
void DoomDebugRecordFrame(unsigned number) {
    SuperFx *fx = g_snes && g_snes->cart ? g_snes->cart->superfx : NULL;
    unsigned ticks = fx && fx->ram && fx->ram_size > 0x1eb
        ? fx->ram[0x1ea] | ((unsigned)fx->ram[0x1eb] << 8) : 0;
    DoomRendererStats snapshot;
    DoomRendererGetStats(&snapshot);
    DoomInputStats input_snapshot;
    DoomInputGetStats(&input_snapshot);
    DoomCheatsStats cheat_snapshot;DoomCheatsGetStats(&cheat_snapshot);
    LOCK();
    stats = snapshot;
    input_stats = input_snapshot;
    cheat_stats=cheat_snapshot;
    music_loading=DoomMusicBusy();
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
    if(!strcmp(cmd,"music_stats")) {
        char json[128];LOCK();int loading=music_loading;unsigned f=frame;UNLOCK();
        snprintf(json,sizeof(json),"{\"ok\":true,\"frame\":%u,\"loading\":%d}",f,loading);
        send(json);return 1;
    }
    if(!strcmp(cmd,"cheat_exit_test")) {
        LOCK();int ready=cheat_stats.enabled && cheat_stats.ready;
        if(ready)requested_level_exit=1;UNLOCK();
        send(ready?"{\"ok\":true,\"queued\":true}":"{\"ok\":false,\"error\":\"gameplay required\"}");return 1;
    }
    if(!strcmp(cmd,"cheat_test") || !strcmp(cmd,"cheat_keyboard_test")) {
        size_t n=strlen(args);int valid=n>=4 && n<=10;
        for(size_t i=0;i<n;i++)if(!((args[i]>='A' && args[i]<='Z') ||
            (args[i]>='a' && args[i]<='z') || (args[i]>='0' && args[i]<='9')))valid=0;
        LOCK();int ready=cheat_stats.enabled && cheat_stats.ready;
        if(valid && ready) {
            memcpy(requested_cheat,args,n+1);
            requested_cheat_keyboard=!strcmp(cmd,"cheat_keyboard_test");
        }
        UNLOCK();
        send(valid && ready?"{\"ok\":true,\"queued\":true}":"{\"ok\":false,\"error\":\"cheat_test requires enabled cheats and gameplay\"}");
        return 1;
    }
    if(!strcmp(cmd,"cheat_stats")) {
        char json[512];LOCK();DoomCheatsStats s=cheat_stats;unsigned f=frame;UNLOCK();
        snprintf(json,sizeof(json),"{\"ok\":true,\"frame\":%u,\"enabled\":%d,\"ready\":%d,\"god\":%d,\"clip\":%d,\"map\":%d,\"health\":%d,\"armor\":%d,\"keys\":%d,\"arms\":%d,\"face\":%d,\"skill\":%d,\"level\":%d,\"x\":%d,\"y\":%d,\"ammo\":[%d,%d,%d,%d],\"monster_arrows\":%u}",
            f,s.enabled,s.ready,s.god,s.clip,s.map,s.health,s.armor,s.keys,s.arms,s.face,s.skill,s.level,s.x,s.y,s.ammo[0],s.ammo[1],s.ammo[2],s.ammo[3],s.monster_arrows);
        send(json);return 1;
    }
    if(!strcmp(cmd,"menu_pointer_test")) {
        int x,y,click;char extra;
        if(sscanf(args,"%d %d %d %c",&x,&y,&click,&extra)!=3 ||
           x<-1024 || x>1024 || y<0 || y>224 || (click!=0 && click!=1)) {
            send("{\"error\":\"usage: menu_pointer_test <native x> <native y> <click 0|1>\"}");return 1;
        }
        LOCK();pointer_x=x;pointer_y=y;pointer_click=click;requested_pointer=1;UNLOCK();
        send("{\"ok\":true,\"queued\":true}");return 1;
    }
    if(!strcmp(cmd,"modern_key_test")) {
        int key=!strcmp(args,"space") ? SDLK_SPACE :
            !strcmp(args,"ctrl") ? SDLK_LCTRL : !strcmp(args,"release-ctrl") ? SDLK_RCTRL :
            !strcmp(args,"+") ? SDLK_PLUS : !strcmp(args,"-") ? SDLK_MINUS :
            strlen(args)==1 && args[0]>='1' && args[0]<='7' ? args[0] : 0;
        LOCK();int ready=input_stats.enabled && input_stats.gameplay;
        if(key && ready)requested_key=key;UNLOCK();
        send(key && ready ? "{\"ok\":true,\"queued\":true}" :
            "{\"ok\":false,\"error\":\"usage during modern gameplay: modern_key_test <space|1..7|ctrl|release-ctrl|+|->\"}");
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
    char json[1792];
    LOCK();
    snprintf(json, sizeof(json),
        "{\"ok\":true,\"frame\":%u,\"p1_input\":%u,\"p2_input\":%u,\"supported\":%s,\"has_snapshot\":%s,"
        "\"captures\":%llu,\"camera_passes\":%llu,\"cache_hits\":%llu,"
        "\"failures\":%llu,\"replay_instructions\":%llu,\"snapshot_interval_fields\":%u,"
        "\"weapon_updates\":%llu,\"weapon_interpolated_presentations\":%llu,"
        "\"weapon_offset_x\":%d,\"weapon_offset_y\":%d,"
        "\"weapon_tiles\":%u,\"weapon_layout_fallbacks\":%llu,\"weapon_translucent\":%s,"
        "\"invisibility_ticks\":%u,\"resolution_scale\":%u,\"resolution_segments\":%u,"
        "\"recovered_edges\":%u,\"menu_active\":%s,"
        "\"automap_active\":%s,\"automap_transparent\":%s,\"automap_lines\":%u,"
        "\"automap_enemy_arrows\":%u,\"automap_projectile_arrows\":%u,\"automap_draws\":%llu,\"automap_interpolated_presentations\":%llu,"
        "\"automap_world_interpolated_presentations\":%llu,"
        "\"look_offset\":%.3f,\"requested_look_offset\":%.3f,\"draw_calls\":%llu,\"draw_total_ms\":%.6f,\"draw_max_ms\":%.6f}",
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
        stats.recovered_edges,stats.menu_active ? "true" : "false",
        stats.automap_active ? "true" : "false",stats.automap_transparent ? "true" : "false",stats.automap_lines,
        stats.automap_enemy_arrows,stats.automap_projectile_arrows,(unsigned long long)stats.automap_draws,(unsigned long long)stats.automap_interpolated_presentations,
        (unsigned long long)stats.automap_world_interpolated_presentations,
        stats.look_offset,stats.requested_look_offset,draws, draw_total_ms, draw_max_ms);
    UNLOCK();
    send(json);
    return 1;
}

void DoomDebugInit(void) { debug_server_set_game_command_handler(Command); }
#endif

/* USA retail DOOM-FX hooks. Addresses checked against rlplayer2.a,
 * rlstatus.a, rlmove4.a and rlautomap.a and guarded by opcode signatures.
 * Commands execute on the emulation thread at the player tick. */
#include "doom_cheats.h"
#include "common_cpu_infra.h"
#include "mod_runtime.h"
#include "snes/cart.h"
#include "snes/superfx.h"
#define SDL_MAIN_HANDLED 1
#include "desktop/sdl_compat.h"
#include <string.h>

static SuperFx *core;
static int enabled, suspended, god, clip, map_mode, map_original;
static unsigned field, last_tick;
static char typed[9];
static unsigned length, pending;
#if SNESRECOMP_TRACE
static unsigned monster_arrows;
#endif
enum { God=1, Arsenal, Keys, Clip, Map, Warp=0x100 };
static unsigned Word(SuperFx *fx,unsigned p) {
    return superfx_ram_peek(fx,p)|(superfx_ram_peek(fx,p+1)<<8);
}
static void Put(SuperFx *fx,unsigned p,unsigned v) {
    fx->ram[p]=(uint8_t)v;fx->ram[p+1]=(uint8_t)(v>>8);
}
static unsigned Player(SuperFx *fx) { return Word(fx,0x18c); }
static int ValidPlayer(SuperFx *fx,unsigned p) {
    return p>=0x5a8e && p+38<=fx->ram_size;
}
static void SelectFace(SuperFx *fx,int invulnerable) {
    unsigned p=Player(fx);if(!ValidPlayer(fx,p))return;
    unsigned health=Word(fx,p+32);
    unsigned anim=invulnerable || Word(fx,0x40)?0xe8b8:
        !health?0xe9ae:0xe8e8+33*(health>=80?0:5-(health>>4));
    Put(fx,0x36,anim);Put(fx,0x38,anim);Put(fx,0x3a,1);
}
/* Missing PC maps are intentionally rejected rather than silently redirected. */
static int Level(unsigned e,unsigned m) {
    static const unsigned masks[]={0,0x3be,0x35a,0x3de};
    return e>=1 && e<=3 && m>=1 && m<=9 && (masks[e]&(1u<<m))
        ? (int)((e-1)*9+m-1) : -1;
}
static void Settings(void) {
    enabled=snes_mod_runtime_feature_enabled_c("doom.cheats","keyboard-cheats");
}
SNES_MOD_CONSTRUCTOR(RegisterDoomCheats) {
    snes_mod_register_activation_plugin("doom.cheats.keyboard",Settings);
    snes_mod_register_reset_callback(Settings);
}
static void Tick(SuperFx *fx,uint32_t pc,void *context) {
    (void)pc;(void)context;
    last_tick=field;
    if(suspended)return;
    unsigned p=Player(fx),command=pending;pending=0;
    if(!ValidPlayer(fx,p))return;
    if(map_mode && !fx->ram[0x46])map_mode=0; /* native level initialization */
#if SNESRECOMP_TRACE
    if(command==0x200) { Put(fx,0x1e8,11);return; }
#endif
    if(command==God) {
        god=!god;
        if(god)Put(fx,p+32,100);
        SelectFace(fx,god);
    } else if(command==Arsenal || command==Keys) {
        static const unsigned ammo[]={200,50,50,300};
        unsigned scale=fx->ram[0x47]?2:1;
        for(unsigned i=0;i<4;i++)Put(fx,0x5e + 2*i,ammo[i]*scale);
        Put(fx,0x1d4,200);fx->ram[0x1d9]=0xff;
        if(command==Keys)fx->ram[0x1d8]=0x3f;
    } else if(command==Clip)clip=!clip;
    else if(command==Map) {
        if(!map_mode)map_original=fx->ram[0x46];
        map_mode=(map_mode+1)%3;
        fx->ram[0x46]=map_mode?1:(uint8_t)map_original;
    } else if(command&Warp) {
        /* ExitLevel $NN52: native RAGE4000 resets inventory and loads NN.
         * The native Level variable and NextLevelI retain normal exits. */
        Put(fx,0x1e8,((command&0xff)<<8)|0x52);
        map_mode=0;length=0;
    }
    if(god && Word(fx,0x38)!=0xe8b8) {
        Put(fx,0x36,0xe8b8);Put(fx,0x38,0xe8b8);Put(fx,0x3a,1);
    }
}
static void Damage(SuperFx *fx,uint32_t pc,void *context) {
    (void)pc;(void)context;
    if(god)superfx_hook_redirect(fx,(uint16_t)superfx_reg(fx,11));
}
static void SubHealth(SuperFx *fx,uint32_t pc,void *context) {
    (void)pc;(void)context;
    if(god)superfx_hook_redirect(fx,0xe83b);
}
static void Face(SuperFx *fx,uint32_t pc,void *context) {
    (void)pc;(void)context;
    if(god) {
        superfx_set_reg(fx,4,0xe8b8);
        superfx_hook_redirect(fx,0xe874);
    }
}
static void AnimateFace(SuperFx *fx,uint32_t pc,void *context) {
    (void)pc;(void)context;
    if(god && Word(fx,0x38)!=0xe8b8) {
        /* Pickups can request a temporary grin after the player tick. */
        Put(fx,0x36,0xe8b8);Put(fx,0x38,0xe8b8);Put(fx,0x3a,1);
    }
}
static void Move(SuperFx *fx,uint32_t pc,void *context) {
    (void)pc;(void)context;
    unsigned p=Word(fx,0x11c); /* MVRObj */
    if(!clip || p!=Player(fx) || !ValidPlayer(fx,p))return;
    /* Native velocity is signed 8.8; object coordinates are signed 16.16.
     * Keep the native sector unlink/BSP lookup/relink and line triggers. */
    Put(fx,0x11e,Word(fx,p+8));Put(fx,0x120,Word(fx,p+12));
    for(unsigned axis=0;axis<2;axis++) {
        unsigned a=p+6+4*axis;
        uint32_t xy=Word(fx,a)|((uint32_t)Word(fx,a+2)<<16);
        xy+=(int32_t)(int16_t)Word(fx,p+16+2*axis)*256;
        Put(fx,a,xy);Put(fx,a+2,xy>>16);
    }
    Put(fx,0x13e,0x7fff); /* MVIPercent */
    superfx_hook_redirect(fx,0xa6b6);
}
static void Sector(SuperFx *fx,uint32_t pc,void *context) {
    (void)pc;(void)context;
    unsigned p=superfx_reg(fx,12),s=superfx_reg(fx,6);
    if(!clip || p!=Player(fx) || !ValidPlayer(fx,p) || s>=256)return;
    unsigned floor=Word(fx,0x3082+14*s),ceiling=Word(fx,0x3084+14*s);
    Put(fx,p+34,floor);Put(fx,p+36,ceiling);
    Put(fx,p+14,floor);
}
static void MapNext(SuperFx *fx,uint32_t pc,void *context) {
    (void)pc;(void)context;
    if(map_mode==2 && (int16_t)superfx_reg(fx,0)>=0)
        superfx_hook_redirect(fx,0xed3b);
}
static void MapObject(SuperFx *fx,uint32_t pc,void *context) {
    (void)pc;(void)context;
    unsigned p=superfx_reg(fx,0);
    if(p==Player(fx))return;
    /* Enemy tags 6..14, living objects only; omit missiles and corpses. */
    if(map_mode!=2 || !ValidPlayer(fx,p) || fx->ram[p+5]<6 ||
       fx->ram[p+5]>14 || (int16_t)Word(fx,p+32)<=0)
        superfx_hook_redirect(fx,0xed2e);
#if SNESRECOMP_TRACE
    else monster_arrows++;
#endif
}
typedef struct CheatHook { unsigned pc; const char *bytes; unsigned size; SuperFxPcHook *fn; } CheatHook;
static const CheatHook hooks[]={
    {0x8184,"\x3d\xa9\xf8\x3d\xa1\x16",6,Tick},
    {0x8501,"\x3d\xa0\x20\xe0\x0a\x46",6,Damage},
    {0xe82e,"\xa0\x20\x3d\xa1\xc6\x51\x40\x64",8,SubHealth},
    {0xe83b,"\xf4\xb8\xe8\x3d\xa0\x20",6,Face},
    {0xe889,"\x3d\xa1\xf7\x3d\xa0\x1d",6,AnimateFace},
    {0xa42f,"\x3d\xac\x8e\xbc\x3e\x56",6,Move},
    {0xa6c3,"\xa0\x16\x5c\xb6\x3d\x30",6,Sector},
    {0xed39,"\x05\xc8\x90\x41\xd1\xd0",6,MapNext},
    {0xed42,"\x3e\xa1\x3c\x3e\x58",5,MapObject}
};
static void Remove(void) {
    if(core)for(unsigned i=0;i<sizeof(hooks)/sizeof(*hooks);i++)
        superfx_set_pc_hook(core,hooks[i].pc,NULL,NULL);
    core=NULL;
}
void DoomCheatsReset(void) {
    god=clip=map_mode=map_original=0;length=pending=0;field=last_tick=0;
#if SNESRECOMP_TRACE
    monster_arrows=0;
#endif
}
void DoomCheatsShutdown(void) { Remove();DoomCheatsReset(); }
void DoomCheatsSuspended(int value) { suspended=value;length=pending=0; }
void DoomCheatsBeforeFrame(void) {
    Settings();++field;
    SuperFx *fx=g_snes && g_snes->cart?g_snes->cart->superfx:NULL;
    if(fx!=core) { core=NULL;DoomCheatsReset(); }
    if(!enabled) {
        if(core && map_mode)core->ram[0x46]=(uint8_t)map_original;
        if(core && god)SelectFace(core,0);
        Remove();DoomCheatsReset();return;
    }
    if(core || !fx || fx->rom_size!=0x200000 || fx->ram_size<0x10000)return;
    for(unsigned i=0;i<sizeof(hooks)/sizeof(*hooks);i++)
        if(memcmp(fx->rom+(hooks[i].pc&0x7fff),hooks[i].bytes,hooks[i].size))return;
    core=fx;
    for(unsigned i=0;i<sizeof(hooks)/sizeof(*hooks);i++)
        if(!superfx_set_pc_hook(fx,hooks[i].pc,hooks[i].fn,NULL)) { Remove();return; }
}
static void Character(int key) {
    if(key>='a' && key<='z')key-='a'-'A';
    if(!((key>='A' && key<='Z') || (key>='0' && key<='9'))) { length=0;return; }
    if(length==8) { memmove(typed,typed+1,7);length--; }
    typed[length++]=(char)key;typed[length]=0;
    static const struct { const char *text;unsigned command; } codes[]={
        {"IDDQD",God},{"IDKFA",Keys},{"IDFA",Arsenal},{"IDCLIP",Clip},{"IDDT",Map}
    };
    for(unsigned i=0;i<sizeof(codes)/sizeof(*codes);i++) {
        unsigned n=(unsigned)strlen(codes[i].text);
        if(length>=n && !strcmp(typed+length-n,codes[i].text)) {
            pending=codes[i].command;length=0;return;
        }
    }
    if(length==8 && !memcmp(typed,"IDCLEV",6)) {
        int level=Level(typed[6]-'0',typed[7]-'0');
        if(level>=0)pending=Warp|(unsigned)level;
        length=0;
    }
}
void DoomCheatsEvent(const void *opaque) {
    const SDL_Event *event=opaque;
#if SNESRECOMP_SDL3
    int lost=event->type==SDL_EVENT_WINDOW_FOCUS_LOST;
#else
    int lost=event->type==SDL_WINDOWEVENT && event->window.event==SDL_WINDOWEVENT_FOCUS_LOST;
#endif
    if(lost) { length=pending=0;return; }
    if(!enabled || !core || suspended || !last_tick || field-last_tick>12 ||
       !SDL_GetKeyboardFocus()) { length=0;return; }
    if(event->type!=SDL_KEYDOWN || event->key.repeat)return;
    int key=SNESRECOMP_SDL_EVENT_KEY(*event);
    if(SNESRECOMP_SDL_EVENT_MOD(*event)&(KMOD_CTRL|KMOD_ALT|KMOD_GUI)) { length=0;return; }
    Character(key);
}
#if SNESRECOMP_TRACE
void DoomCheatsTestExit(void) {
    if(enabled && core && !suspended && last_tick && field-last_tick<=12)pending=0x200;
}
void DoomCheatsTestText(const char *text) {
    if(!enabled || !core || suspended || !last_tick || field-last_tick>12)return;
    while(*text)Character((unsigned char)*text++);
}
void DoomCheatsGetStats(DoomCheatsStats *s) {
    memset(s,0,sizeof(*s));s->enabled=enabled;s->ready=core && last_tick && field-last_tick<=12;
    s->god=god;s->clip=clip;s->map=map_mode;
    s->monster_arrows=monster_arrows;
    if(!core)return;
    unsigned p=Player(core);if(!ValidPlayer(core,p))return;
    s->health=Word(core,p+32);s->armor=Word(core,0x1d4);
    s->keys=core->ram[0x1d8];s->arms=core->ram[0x1d9];s->face=Word(core,0x1d6);
    s->skill=Word(core,0);s->level=g_snes->ram[0x6d1];
    s->x=(int16_t)Word(core,p+8);s->y=(int16_t)Word(core,p+12);
    for(unsigned i=0;i<4;i++)s->ammo[i]=Word(core,0x5e + 2*i);
}
#endif

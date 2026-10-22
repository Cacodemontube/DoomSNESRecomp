/* Optional gameplay hooks for the USA retail DOOM-FX engine.
 * Layout/call contracts: Randy Linden's rlmobjects3.a, rlenemies5/6.a,
 * rlprojectiles.a, rlradius.a and rlweapons3.a. Retaliation/source ownership
 * follow the distinction in id's P_DamageMobj; SNES attacks remain native.
 */
#include "doom_infighting.h"
#include "common_cpu_infra.h"
#include "mod_runtime.h"
#include "snes/cart.h"
#include "snes/superfx.h"
#include "snes/saveload.h"
#include <string.h>
#include <math.h>

enum { Objects=0x5a8e, Stride=38, Count=100, RadiusBase=0x2b80, RadiusCount=14 };
typedef struct Origin { uint16_t object, species, missile; } Origin;
typedef struct InfightingState {
    uint32_t level;
    Origin missiles[Count], radius[RadiusCount];
} InfightingState;
static InfightingState state;
static SuperFx *core;
static int enabled;
#if SNESRECOMP_TRACE
static DoomInfightingStats stats;
#define TALLY(field) (++stats.field)
void DoomInfightingGetStats(DoomInfightingStats *out) { *out=stats;out->ready=core!=NULL; }
#else
#define TALLY(field) ((void)0)
#endif
static unsigned Word(SuperFx *fx,unsigned p) {
    return superfx_ram_peek(fx,p)|(superfx_ram_peek(fx,p+1)<<8);
}
static void Put(SuperFx *fx,unsigned p,unsigned v) {
    for(unsigned i=0;i<2;i++) {
        unsigned a=p+i;uint8_t b=(uint8_t)(v>>(8*i));
        fx->ram[a]=b;
        if(fx->ramcl && fx->ramar==a)fx->ramdr=b;
    }
}
static int Slot(unsigned p) {
    return p>=Objects && p<Objects+Stride*Count && (p-Objects)%Stride==0
        ?(int)((p-Objects)/Stride):-1;
}
static unsigned Type(SuperFx *fx,unsigned p) { return superfx_ram_peek(fx,p+5); }
static int Alive(SuperFx *fx,unsigned p) {
    return Slot(p)>=0 && Word(fx,p)!=0xffff &&
        (int16_t)Word(fx,p+32)>0 && (superfx_ram_peek(fx,p+4)&0x40);
}
static int Monster(SuperFx *fx,unsigned p) {
    return Slot(p)>=0 && Type(fx,p)>=6 && Type(fx,p)<=14 && Alive(fx,p);
}
static unsigned Player(SuperFx *fx) { return Word(fx,0x18c); }
static void Level(SuperFx *fx) {
    uint32_t level=Word(fx,0x7c)|((uint32_t)Word(fx,0x7e)<<16);
    if(state.level!=level) { memset(&state,0,sizeof(state));state.level=level; }
}
static Origin Source(SuperFx *fx,unsigned p) {
    Origin o={0,0,0};
    if(Monster(fx,p) || (p==Player(fx) && Alive(fx,p))) {
        o.object=(uint16_t)p;o.species=(uint16_t)Type(fx,p);
    }
    return o;
}
static Origin MissileSource(SuperFx *fx,unsigned p) {
    Origin none={0,0,0};int slot=Slot(p);
    if(slot<0)return none;
    Origin o=state.missiles[slot];
    return o.missile==Type(fx,p)?o:none;
}
static void Retaliate(SuperFx *fx,unsigned victim,Origin source) {
    if(!Monster(fx,victim) || victim==source.object || !Alive(fx,source.object) ||
       Type(fx,source.object)!=source.species ||
       !(Monster(fx,source.object) || source.object==Player(fx)))return;
    /* Native target acquisition checks health, REJECT and BSP sight, then
     * computes facing/distance and drives the existing attacks. */
    if(Word(fx,victim+34)!=source.object && source.species>=6)TALLY(retaliations);
    Put(fx,victim+34,source.object);
}
static int Immune(SuperFx *fx,unsigned victim,Origin o,int melee) {
    if(!Monster(fx,victim) || !o.object)return 0;
    if(victim==o.object && o.missile)return 1;
    if(Type(fx,victim)!=o.species)return 0;
    if(melee)return o.species==9 || o.species==11;
    return (o.species==8 && o.missile==64) ||
           (o.species==10 && o.missile==65) ||
           (o.species==12 && o.missile==66) ||
           (o.species==13 && o.missile==67);
}
static void Birth(SuperFx *fx,uint32_t pc,void *unused) {
    (void)pc;(void)unused;Level(fx);
    unsigned p=superfx_reg(fx,12);int slot=Slot(p);if(slot<0)return;
    Origin o=Source(fx,superfx_reg(fx,10));o.missile=(uint16_t)Type(fx,p);
    state.missiles[slot]=o;
    TALLY(births);
}
/* Enemy gunfire in DOOM-FX damages its intended target directly. Intercept
 * successful native shots at the first living monster along that sight line.
 * No per-frame world scan, no new RNG, no replacement of native BSP sight. */
static unsigned Intercept(SuperFx *fx,unsigned shooter,unsigned target) {
    if(!Alive(fx,target))return target;
    double sx=(int16_t)Word(fx,shooter+8),sy=(int16_t)Word(fx,shooter+12);
    double dx=(int16_t)(Word(fx,target+8)-Word(fx,shooter+8));
    double dy=(int16_t)(Word(fx,target+12)-Word(fx,shooter+12));
    double length=dx*dx+dy*dy;if(length<1)return target;
    double sz=(int16_t)Word(fx,shooter+14)+42;
    double dz=(int16_t)Word(fx,target+14)+28-sz,best=1;
    unsigned result=target;
    static const unsigned radius[]={20,20,20,30,31,16,24,35,96};
    static const unsigned height[]={56,56,56,56,56,56,56,96,88};
    for(unsigned p=Objects;p<Objects+Stride*Count;p+=Stride) {
        if(p==shooter || p==target || !Monster(fx,p))continue;
        unsigned type=Type(fx,p)-6;
        double x=(int16_t)(Word(fx,p+8)-(unsigned)(int)sx);
        double y=(int16_t)(Word(fx,p+12)-(unsigned)(int)sy);
        double t=(x*dx+y*dy)/length;if(t<=0 || t>=1)continue;
        double perpendicular=x*x+y*y-t*t*length,r=radius[type];
        if(perpendicular>r*r)continue;
        double entry=t-sqrt(fmax(0,(r*r-perpendicular)/length));
        if(entry<0)entry=0;
        double z=sz+dz*t,bottom=(int16_t)Word(fx,p+14);
        if(z<bottom || z>bottom+height[type])continue;
        if(entry<best) { best=entry;result=p; }
    }
    return result;
}
static void Damage(SuperFx *fx,uint32_t pc,void *unused) {
    (void)unused;Level(fx);
    unsigned attacker=superfx_reg(fx,12),victim=superfx_reg(fx,13);
    TALLY(hits);
    Origin o=Source(fx,attacker);int melee=pc==0x99c5;
    if(pc==0x9d1d)o=MissileSource(fx,attacker);
    else if(pc==0x99ec && Monster(fx,attacker)) {
        unsigned intercepted=Intercept(fx,attacker,victim);
        if(intercepted!=victim)TALLY(interceptions);
        victim=intercepted;superfx_set_reg(fx,13,victim);
    }
    if(Immune(fx,victim,o,melee)) {
        TALLY(blocked);
        superfx_hook_redirect(fx,(uint16_t)superfx_reg(fx,11));return;
    }
    Retaliate(fx,victim,o);
}
static void PlayerDamage(SuperFx *fx,uint32_t pc,void *unused) {
    (void)pc;(void)unused;Level(fx);
    Retaliate(fx,superfx_reg(fx,13),Source(fx,Player(fx)));
}
static void EnqueueRadius(SuperFx *fx,uint32_t pc,void *unused) {
    (void)pc;(void)unused;Level(fx);
    unsigned p=superfx_reg(fx,7),object=superfx_reg(fx,12);
    if(p<RadiusBase || p>=RadiusBase+RadiusCount*6 || (p-RadiusBase)%6)return;
    state.radius[(p-RadiusBase)/6]=MissileSource(fx,object);
}
static void RadiusDamage(SuperFx *fx,uint32_t pc,void *unused) {
    (void)pc;(void)unused;Level(fx);
    unsigned p=(uint16_t)(Word(fx,0x17e)-6),victim=superfx_reg(fx,13);
    if(p<RadiusBase || p>=RadiusBase+RadiusCount*6 || (p-RadiusBase)%6)return;
    Origin o=state.radius[(p-RadiusBase)/6];
    TALLY(splash);
    /* Cyberdemons are protected from other Cyberdemons' rockets, including
     * splash; the player's rocket launcher retains its normal damage. */
    if(o.species==13 && o.missile==67 && Monster(fx,victim) && Type(fx,victim)==13) {
        TALLY(blocked);
        superfx_hook_redirect(fx,(uint16_t)superfx_reg(fx,11));return;
    }
    Retaliate(fx,victim,o);
}
typedef struct Hook { unsigned pc;const char *bytes;unsigned size;SuperFxPcHook *fn; } Hook;
static const Hook hooks[]={
    {0x99c5,"\xff\xc8\x91\x01",4,Damage},
    {0x99ec,"\xff\xc8\x91\x01",4,Damage},
    {0x9d1d,"\xff\xc8\x91\x01",4,Damage},
    {0x8c99,"\xff\xd7\x91\x01",4,PlayerDamage},
    {0x9b53,"\xba\x13\x3e\x58\xbc\x12",6,Birth},
    {0x8b72,"\xba\x13\x3e\x58\xbc\x12",6,Birth},
    {0x90de,"\x37\xd7\xd7\xbc\x3e\x58",6,EnqueueRadius},
    {0x9dc1,"\xff\xd7\x91\x01\x3d\xa0\xbf",7,RadiusDamage}
};
static void Settings(void) {
    enabled=snes_mod_runtime_feature_enabled_c("doom.infighting","monster-infighting");
}
SNES_MOD_CONSTRUCTOR(RegisterDoomInfighting) {
    snes_mod_register_activation_plugin("doom.infighting.native",Settings);
    snes_mod_register_reset_callback(Settings);
}
static void Remove(void) {
    if(core)for(unsigned i=0;i<sizeof(hooks)/sizeof(*hooks);i++)
        superfx_set_pc_hook(core,hooks[i].pc,NULL,NULL);
    core=NULL;
}
void DoomInfightingReset(void) { memset(&state,0,sizeof(state)); }
void DoomInfightingShutdown(void) { Remove();DoomInfightingReset(); }
void DoomInfightingBeforeFrame(void) {
    Settings();SuperFx *fx=g_snes && g_snes->cart?g_snes->cart->superfx:NULL;
    if(fx!=core) { core=NULL;DoomInfightingReset(); }
    if(!enabled) { Remove();DoomInfightingReset();return; }
    if(core || !fx || (fx->rom_size!=0x200000 && fx->rom_size!=0x400000) || fx->ram_size<0x10000)return;
    for(unsigned i=0;i<sizeof(hooks)/sizeof(*hooks);i++)
        if(memcmp(fx->rom+(hooks[i].pc&0x7fff),hooks[i].bytes,hooks[i].size))return;
    core=fx;
    for(unsigned i=0;i<sizeof(hooks)/sizeof(*hooks);i++)
        if(!superfx_set_pc_hook(fx,hooks[i].pc,hooks[i].fn,NULL)) { Remove();return; }
}
void DoomInfightingSave(SaveLoadInfo *sli) { sli->func(sli,&state,sizeof(state)); }
bool DoomInfightingLoad(SaveLoadInfo *sli) {
    sli->func(sli,&state,sizeof(state));
    /* State blobs are untrusted. Validate every saved pointer and type. */
    for(unsigned i=0;i<Count+RadiusCount;i++) {
        Origin o=i<Count?state.missiles[i]:state.radius[i-Count];
        if(o.object && (Slot(o.object)<0 || (o.species!=0 && (o.species<6 || o.species>14)) ||
           o.missile<64 || o.missile>69)) { DoomInfightingReset();return false; }
    }
    return true;
}

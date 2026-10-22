/* Exercise production hooks at retail GSU call sites, not a second policy
 * implementation. The ROM remains user supplied and is never distributed. */
#include "../src/doom_infighting.c"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
Snes *g_snes;
static int selected;
int snes_mod_runtime_feature_enabled_c(const char *p,const char *f) {
    CHECK(!strcmp(p,"doom.infighting") && !strcmp(f,"monster-infighting"));return selected;
}
int snes_mod_register_activation_plugin(const char *id,SNESModActivationCallback cb) {
    CHECK(!strcmp(id,"doom.infighting.native") && cb);return 1;
}
int snes_mod_register_reset_callback(SNESModActivationCallback cb) {CHECK(cb);return 1;}
static unsigned damage_seen,victim_seen,damage_amount;
static void Observe(SuperFx *fx,uint32_t pc,void *unused) {
    (void)pc;(void)unused;damage_seen++;victim_seen=superfx_reg(fx,13);
    damage_amount=superfx_reg(fx,4);superfx_hook_redirect(fx,0xf800);
}
static void Run(SuperFx *fx,unsigned pc) {
    superfx_cpu_write_io(fx,0x301e,(uint8_t)pc);
    superfx_cpu_write_io(fx,0x301f,(uint8_t)(pc>>8));
    superfx_sync(fx,fx->master_clock+100000);CHECK(!superfx_is_running(fx));
}
static void Object(SuperFx *fx,unsigned p,unsigned type,int x,int y) {
    memset(fx->ram+p,0,Stride);Put(fx,p,0);fx->ram[p+4]=0xe8;fx->ram[p+5]=(uint8_t)type;
    Put(fx,p+8,x);Put(fx,p+12,y);Put(fx,p+32,100);Put(fx,p+34,Objects);
}
static void Prepare(SuperFx *fx) {
    superfx_reset(fx);memset(fx->ram,0,65536);DoomInfightingReset();
    Put(fx,0x18c,Objects);Object(fx,Objects,0,500,0);
    damage_seen=victim_seen=0;superfx_set_reg(fx,11,0xf800);
}
static void Shot(SuperFx *fx,unsigned pc,unsigned attacker,unsigned victim) {
    superfx_set_reg(fx,11,0xf800);superfx_set_reg(fx,12,attacker);superfx_set_reg(fx,13,victim);
    superfx_set_reg(fx,4,pc==0x99c5?11:13);Run(fx,pc);
}
static void Own(SuperFx *fx,unsigned owner,unsigned projectile,unsigned type) {
    Object(fx,projectile,type,0,0);superfx_set_reg(fx,10,owner);superfx_set_reg(fx,12,projectile);
    Birth(fx,0,NULL);
}
typedef struct Stream {SaveLoadInfo sli;InfightingState blob;int load;} Stream;
static void Transfer(SaveLoadInfo *sli,void *data,size_t size) {
    Stream *s=(Stream *)sli;CHECK(size==sizeof(s->blob));
    if(s->load)memcpy(data,&s->blob,size);else memcpy(&s->blob,data,size);
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=malloc(0x200000),*ram=malloc(65536);CHECK(rom && ram);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);fclose(f);
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    Snes snes={0};Cart cart={0};snes.cart=&cart;cart.superfx=fx;g_snes=&snes;
    selected=0;DoomInfightingBeforeFrame();CHECK(!core && fx->pc_hook_count==0);
    selected=1;DoomInfightingBeforeFrame();
    if(!core)for(unsigned i=0;i<sizeof(hooks)/sizeof(*hooks);i++)
        if(memcmp(rom+(hooks[i].pc&0x7fff),hooks[i].bytes,hooks[i].size))fprintf(stderr,"bad guard %04x\n",hooks[i].pc);
    CHECK(core==fx && fx->pc_hook_count==sizeof(hooks)/sizeof(*hooks));
    rom[0x7800]=0;rom[0x7801]=1;
    CHECK(superfx_set_pc_hook(fx,0x91c8,Observe,NULL));
    CHECK(superfx_set_pc_hook(fx,0x91d7,Observe,NULL));
    unsigned a=Objects+38,b=Objects+76,missile=Objects+114;
    /* Run both actual coordinate-birth tails to prove hook timing sees the
     * initialized type, including the GSU's buffered RAM writes. */
    CHECK(superfx_set_pc_hook(fx,0x9b88,Observe,NULL));
    CHECK(superfx_set_pc_hook(fx,0x8ba7,Observe,NULL));
    Prepare(fx);Object(fx,a,8,0,0);Object(fx,missile,64,0,0);
    superfx_set_reg(fx,10,a);superfx_set_reg(fx,12,missile);Run(fx,0x9b53);
    CHECK(state.missiles[3].object==a && state.missiles[3].missile==64);
    superfx_set_reg(fx,10,Objects);superfx_set_reg(fx,12,missile);Run(fx,0x8b72);
    CHECK(state.missiles[3].object==Objects);
    CHECK(superfx_set_pc_hook(fx,0x9b88,NULL,NULL));
    CHECK(superfx_set_pc_hook(fx,0x8ba7,NULL,NULL));
    unsigned cases=0;
    for(unsigned attacker=6;attacker<=14;attacker++)for(unsigned victim=6;victim<=14;victim++) {
        Prepare(fx);Object(fx,a,attacker,0,0);Object(fx,b,victim,100,0);
        Shot(fx,0x99c5,a,b);
        int immune=attacker==victim && (attacker==9 || attacker==11);
        CHECK(damage_seen==!immune);CHECK(Word(fx,b+34)==(immune?Objects:a));cases++;
    }
    const unsigned shooters[]={8,10,12,13},types[]={64,65,66,67};
    for(unsigned s=0;s<4;s++)for(unsigned victim=6;victim<=14;victim++) {
        Prepare(fx);Object(fx,a,shooters[s],0,0);Object(fx,b,victim,100,0);Own(fx,a,missile,types[s]);
        Shot(fx,0x9d1d,missile,b);int immune=victim==shooters[s];
        CHECK(damage_seen==!immune);CHECK(Word(fx,b+34)==(immune?Objects:a));cases++;
    }
    /* Every enemy's successful gunfire can intercept another monster,
     * including the same species. An offset/above/dead item cannot intercept. */
    for(unsigned gun=6;gun<=14;gun++)if(gun==6 || gun==7 || gun==14) {
        Prepare(fx);Object(fx,a,gun,0,0);Object(fx,b,gun,200,0);
        Shot(fx,0x99ec,a,Objects);CHECK(damage_seen==1 && victim_seen==b && Word(fx,b+34)==a);cases++;
        Put(fx,b+12,200);damage_seen=0;Shot(fx,0x99ec,a,Objects);CHECK(victim_seen==Objects);
        Put(fx,b+12,0);Put(fx,b+14,200);Shot(fx,0x99ec,a,Objects);CHECK(victim_seen==Objects);
        Put(fx,b+14,0);Put(fx,b+32,0);Shot(fx,0x99ec,a,Objects);CHECK(victim_seen==Objects);
        Object(fx,b,15,200,0);Shot(fx,0x99ec,a,Objects);CHECK(victim_seen==Objects);
    }
    /* Player reuses a former Cyberdemon rocket slot: no inherited immunity. */
    Prepare(fx);Object(fx,a,13,0,0);Object(fx,b,13,100,0);Own(fx,a,missile,67);
    Own(fx,Objects,missile,67);Put(fx,b+34,a);Shot(fx,0x9d1d,missile,b);
    CHECK(damage_seen==1 && Word(fx,b+34)==Objects);
    /* Successful player gunfire takes aggro back from a monster. */
    Put(fx,b+34,a);superfx_set_reg(fx,13,b);superfx_set_reg(fx,11,0xf800);Run(fx,0x8c99);
    CHECK(Word(fx,b+34)==Objects);
    /* Radius provenance survives freeing/reusing the projectile record. */
    Prepare(fx);Object(fx,a,13,0,0);Object(fx,b,13,100,0);Own(fx,a,missile,67);
    superfx_set_reg(fx,7,RadiusBase);EnqueueRadius(fx,0,NULL);Own(fx,Objects,missile,67);
    Put(fx,0x17e,RadiusBase+6);superfx_set_reg(fx,13,b);superfx_set_reg(fx,11,0xf800);Run(fx,0x9dc1);
    CHECK(damage_seen==0 && Word(fx,b+34)==Objects);
    Object(fx,b,12,100,0);superfx_set_reg(fx,13,b);superfx_set_reg(fx,11,0xf800);Run(fx,0x9dc1);
    CHECK(damage_seen==1 && Word(fx,b+34)==a);
    Stream stream={0};stream.sli.func=Transfer;DoomInfightingSave(&stream.sli);
    InfightingState saved=state;DoomInfightingReset();stream.load=1;
    CHECK(DoomInfightingLoad(&stream.sli) && !memcmp(&state,&saved,sizeof(state)));
    stream.blob.radius[0].object=0xffff;CHECK(!DoomInfightingLoad(&stream.sli));
    /* Real native damage arithmetic after our call hook (trampoline wired as
     * at startup). Stop after the health write, before pain-state dispatch. */
    CHECK(superfx_set_pc_hook(fx,0x91c8,NULL,NULL));CHECK(superfx_set_pc_hook(fx,0x91d7,NULL,NULL));
    CHECK(superfx_set_pc_hook(fx,0x9234,Observe,NULL));
    Prepare(fx);Object(fx,a,8,0,0);Object(fx,b,12,100,0);Own(fx,a,missile,64);
    Put(fx,0x1aa,0x91ef);Put(fx,missile+30,0xf000);
    /* _MObjData bank $3f: inject only a test metadata damage pair (5, range0). */
    rom[0x1ff00d]=0;rom[0x1ff00e]=5;
    Shot(fx,0x9d1d,missile,b);
    CHECK(Word(fx,b+32)==95 && Word(fx,b+34)==a);
    selected=0;DoomInfightingBeforeFrame();CHECK(!core);
    CHECK(fx->pc_hook_count==1); /* only the test health-write observer */
    selected=1;rom[0x19c5]^=1;DoomInfightingBeforeFrame();CHECK(!core);rom[0x19c5]^=1;
    DoomInfightingShutdown();superfx_destroy(fx);free(rom);free(ram);
    printf("Infighting: %u attack matrix cases, gun interception, ownership, radius, save/load and native damage passed\n",cases);
    return 0;
}

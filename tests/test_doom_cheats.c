/* Production parser, retail opcode guards and execution in the real GSU. */
#define SDL_MAIN_HANDLED 1
#include "desktop/sdl_compat.h"
static SDL_Window *focus=(SDL_Window *)1;
static SDL_Window *TestFocus(void) { return focus; }
#define SDL_GetKeyboardFocus TestFocus
#include "../src/doom_cheats.c"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1); } } while(0)
Snes *g_snes;
static int selected;
int snes_mod_runtime_feature_enabled_c(const char *p,const char *f) {
    CHECK(!strcmp(p,"doom.cheats") && !strcmp(f,"keyboard-cheats"));return selected;
}
int snes_mod_register_activation_plugin(const char *id,SNESModActivationCallback cb) {
    CHECK(!strcmp(id,"doom.cheats.keyboard") && cb);return 1;
}
int snes_mod_register_reset_callback(SNESModActivationCallback cb) { CHECK(cb);return 1; }
static void Type(const char *s) {
    SDL_Event e;memset(&e,0,sizeof(e));e.type=SDL_KEYDOWN;
    while(*s) {
#if SNESRECOMP_SDL3
        e.key.key=(SDL_Keycode)*s++;
#else
        e.key.keysym.sym=(SDL_Keycode)*s++;
#endif
        DoomCheatsEvent(&e);
    }
}
static void Command(SuperFx *fx,const char *s) { Tick(fx,0,NULL);Type(s);Tick(fx,0,NULL); }
static void Run(SuperFx *fx,unsigned pc) {
    superfx_cpu_write_io(fx,0x301e,(uint8_t)pc);
    superfx_cpu_write_io(fx,0x301f,(uint8_t)(pc>>8));
    superfx_sync(fx,fx->master_clock+100000);
    CHECK(!superfx_is_running(fx));
}
int main(int argc,char **argv) {
    uint8_t *rom=calloc(1,0x200000),*ram=calloc(1,0x10000);CHECK(rom && ram);
    if(argc>1) {
        FILE *f=fopen(argv[1],"rb");
        if(f) { CHECK(fread(rom,1,0x200000,f)==0x200000);fclose(f); }
        else { puts("Retail ROM unavailable: testing synthetic hook signatures");argc=1; }
    }
    if(argc==1)for(unsigned i=0;i<sizeof(hooks)/sizeof(*hooks);i++)
        memcpy(rom+(hooks[i].pc&0x7fff),hooks[i].bytes,hooks[i].size);
    for(unsigned i=0;i<sizeof(hooks)/sizeof(*hooks);i++) {
        if(memcmp(rom+(hooks[i].pc&0x7fff),hooks[i].bytes,hooks[i].size))
            fprintf(stderr,"Retail guard mismatch: %04x\n",hooks[i].pc);
        CHECK(!memcmp(rom+(hooks[i].pc&0x7fff),hooks[i].bytes,hooks[i].size));
    }
    SuperFx *fx=superfx_create(rom,0x200000,ram,0x10000);CHECK(fx);
    Snes machine={0};Cart cart={0};machine.cart=&cart;cart.superfx=fx;g_snes=&machine;
    unsigned p=0x5a8e;Put(fx,0x18c,p);Put(fx,p+32,37);
    DoomCheatsBeforeFrame();CHECK(!fx->pc_hook_count);
    selected=1;DoomCheatsBeforeFrame();CHECK(fx->pc_hook_count==9);
    DoomCheatsBeforeFrame();
    Type("iddqd");CHECK(!pending); /* menus have no player tick */
    Command(fx,"garbageIDDQD");CHECK(god && Word(fx,p+32)==100);
    if(argc>1) {
        rom[0x7800]=0;rom[0x7801]=1; /* controlled native return */
        superfx_set_reg(fx,11,0xf800);superfx_set_reg(fx,4,50);
        Put(fx,0x1d4,200);Run(fx,0x8501);
        CHECK(Word(fx,p+32)==100 && Word(fx,0x1d4)==200);
        superfx_set_reg(fx,11,0xf800);Run(fx,0xe83b);
        CHECK(Word(fx,0x36)==0xe8b8 && Word(fx,0x38)==0xe8b8);
        Put(fx,0x38,0xe8bb);Put(fx,0x1ee,1);
        superfx_set_reg(fx,11,0xf800);Run(fx,0xe889);
        CHECK(Word(fx,0x1d6)==2); /* pickups cannot replace god eyes */
        CHECK(!Word(fx,0x40)); /* no invulnerability palette */
        superfx_set_reg(fx,11,0xf800);superfx_set_reg(fx,4,50);Run(fx,0xe82e);
        CHECK(Word(fx,p+32)==100);
    }
    Command(fx,"iddqd");CHECK(!god && Word(fx,p+32)==100);
    if(argc>1) {
        superfx_set_reg(fx,11,0xf800);superfx_set_reg(fx,4,20);Run(fx,0xe82e);
        CHECK(Word(fx,p+32)==80); /* damage resumes without restoring old health */
        superfx_set_reg(fx,11,0xf800);Run(fx,0xe83b);
        CHECK(Word(fx,0x36)!=0xe8b8);
    }
    ram[0x1d8]=2;Command(fx,"idfa");CHECK(ram[0x1d8]==2 && ram[0x1d9]==255);
    CHECK(Word(fx,0x1d4)==200 && Word(fx,0x5e)==200 && Word(fx,0x64)==300);
    ram[0x47]=1;Command(fx,"idkfa");CHECK(ram[0x1d8]==63);
    CHECK(Word(fx,0x5e)==400 && Word(fx,0x60)==100 && Word(fx,0x62)==100 && Word(fx,0x64)==600);
    Command(fx,"idclip");CHECK(clip);
    Put(fx,0x11c,p);Put(fx,p+6,0xff00);Put(fx,p+8,10);
    Put(fx,p+10,0);Put(fx,p+12,20);Put(fx,p+16,0x180);Put(fx,p+18,0xff00);
    Move(fx,0,NULL);CHECK(fx->redirect_pending && fx->redirect_pc==0xa6b6);
    CHECK(Word(fx,p+6)==0x7f00 && Word(fx,p+8)==12 && Word(fx,p+12)==19);
    superfx_set_reg(fx,12,p);superfx_set_reg(fx,6,2);
    Put(fx,0x3082+28,24);Put(fx,0x3084+28,128);Sector(fx,0,NULL);
    CHECK(Word(fx,p+34)==24 && Word(fx,p+36)==128 && Word(fx,p+14)==24);
    Command(fx,"idclip");CHECK(!clip);
    Command(fx,"iddt");CHECK(map_mode==1 && ram[0x46]==1);
    Command(fx,"iddt");CHECK(map_mode==2);
    fx->redirect_pending=false;superfx_set_reg(fx,0,2);MapNext(fx,0,NULL);
    CHECK(fx->redirect_pending && fx->redirect_pc==0xed3b);
    fx->redirect_pending=false;superfx_set_reg(fx,0,0xffff);MapNext(fx,0,NULL);CHECK(!fx->redirect_pending);
    unsigned enemy=p+64;ram[enemy+5]=6;Put(fx,enemy+32,20);superfx_set_reg(fx,0,enemy);
    MapObject(fx,0,NULL);CHECK(!fx->redirect_pending);
    Put(fx,enemy+32,0);MapObject(fx,0,NULL);CHECK(fx->redirect_pending);
    Command(fx,"iddt");CHECK(!map_mode && !ram[0x46]);
    static const char *warps[]={"11","12","13","14","15","17","18","19","21","23","24","26","28","29","31","32","33","34","36","37","38","39"};
    for(unsigned i=0;i<sizeof(warps)/sizeof(*warps);i++) {
        char text[9]="IDCLEV";strcat(text,warps[i]);Put(fx,0,4);
        Command(fx,text);CHECK(Word(fx,0x1e8)==((unsigned)Level(warps[i][0]-'0',warps[i][1]-'0')<<8|0x52));
        CHECK(Word(fx,0)==4);
    }
    Command(fx,"IDCLEV16");CHECK(!pending);
    Type("IDD");
    SDL_Event repeat;memset(&repeat,0,sizeof(repeat));repeat.type=SDL_KEYDOWN;repeat.key.repeat=1;
#if SNESRECOMP_SDL3
    repeat.key.key=SDLK_q;
#else
    repeat.key.keysym.sym=SDLK_q;
#endif
    DoomCheatsEvent(&repeat);CHECK(length==3 && !pending);
    repeat.key.repeat=0;
#if SNESRECOMP_SDL3
    repeat.key.mod=KMOD_CTRL;
#else
    repeat.key.keysym.mod=KMOD_CTRL;
#endif
    DoomCheatsEvent(&repeat);CHECK(!length && !pending);
    Type("IDD");DoomCheatsSuspended(1);Type("QD");CHECK(!pending);
    DoomCheatsSuspended(0);Type("QD");CHECK(!pending);
    focus=NULL;Type("IDDQD");CHECK(!pending);focus=(SDL_Window *)1;
    field+=13;Type("IDDQD");CHECK(!pending);
    Command(fx,"IDDQD");CHECK(god);
    Command(fx,"IDDT");CHECK(map_mode==1);
    selected=0;DoomCheatsBeforeFrame();CHECK(!fx->pc_hook_count && !god && !clip);
    CHECK(Word(fx,0x36)!=0xe8b8 && !ram[0x46]);
    selected=1;rom[0x501]^=1;DoomCheatsBeforeFrame();CHECK(!fx->pc_hook_count);
    DoomCheatsShutdown();superfx_destroy(fx);free(rom);free(ram);
    puts("Cheats: parser, native god damage/face, inventory, noclip, map arrows, all 22 warps and lifecycle passed");
    return 0;
}

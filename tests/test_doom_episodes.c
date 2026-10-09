#include "../src/doom_episodes.c"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1); } } while(0)
Snes *g_snes;
static int selected;
int snes_mod_runtime_feature_enabled_c(const char *p,const char *f) {
    CHECK(!strcmp(p,"doom.episodes") && !strcmp(f,"unlocked-episodes"));return selected;
}
int snes_mod_register_activation_plugin(const char *id,SNESModActivationCallback cb) {
    CHECK(!strcmp(id,"doom.episodes.unlocked") && cb);return 1;
}
int snes_mod_register_reset_callback(SNESModActivationCallback cb) { CHECK(cb);return 1; }
int main(int argc,char **argv) {
    uint8_t *rom=calloc(1,0x200000),*original=malloc(0x200000);CHECK(rom && original);
    FILE *file=argc>1?fopen(argv[1],"rb"):NULL;
    if(file) {CHECK(fread(rom,1,0x200000,file)==0x200000);fclose(file);}
    else {
        memcpy(rom+MenuInit,menu_original,sizeof(menu_original));
        memcpy(rom+EpisodeGate,gate_original,sizeof(gate_original));
    }
    memcpy(original,rom,0x200000);
    Snes machine={0};Cart cart={0};machine.cart=&cart;cart.rom=rom;cart.romSize=0x200000;g_snes=&machine;
    DoomEpisodesBeforeFrame();CHECK(!memcmp(rom,original,0x200000));
    selected=1;DoomEpisodesBeforeFrame();CHECK(patched_rom==rom);
    CHECK(rom[MenuInit+1]==3 && rom[MenuInit+2]==0x80);
    CHECK(MenuInit+4+rom[MenuInit+3]==MenuInit+23);
    CHECK(rom[EpisodeGate+5]==0x80 && rom[EpisodeGate+6]==0x0b);
    unsigned changed=0;for(unsigned i=0;i<0x200000;i++)changed+=rom[i]!=original[i];CHECK(changed==4);
    DoomEpisodesBeforeFrame();CHECK(patched_rom==rom);
    selected=0;DoomEpisodesBeforeFrame();CHECK(!memcmp(rom,original,0x200000));
    selected=1;rom[EpisodeGate]^=1;DoomEpisodesBeforeFrame();CHECK(!patched_rom && rom[MenuInit+1]==1);
    rom[EpisodeGate]^=1;DoomEpisodesBeforeFrame();CHECK(patched_rom==rom);
    DoomEpisodesShutdown();CHECK(!memcmp(rom,original,0x200000));
    free(rom);free(original);puts("Episode unlock: retail guards, complete menu height, progression bypass, exact restore passed");
    return 0;
}

#include "doom_crosshair.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static uint8_t ram[65536],before[65536];
static void word(unsigned p,unsigned n) {ram[p]=n;ram[p+1]=n>>8;}
static void candidate(unsigned v,unsigned obj,unsigned depth,unsigned type) {
    word(v,depth);word(v+4,obj);word(v+8,(unsigned)-20);
    word(v+10,100);word(v+12,58);ram[v+14]=56;ram[v+20]=1;
    ram[obj+4]=0xe0;ram[obj+5]=type;
}
int main(void) {
    CHECK(!DoomAimSelect(NULL,0).object);
    word(0xda,0x1000);word(0x1dc,2);
    candidate(0x1000,0x2000,100,6);
    candidate(0x1100,0x2100,150,7);word(0x1002,0x1100);
    memcpy(before,ram,sizeof(ram));
    CHECK(DoomAimSelect(ram,sizeof(ram)).object==0x2000);
    CHECK(!memcmp(before,ram,sizeof(ram))); // preview cannot change simulation/RNG
    ram[0x1014]=0;CHECK(DoomAimSelect(ram,sizeof(ram)).object==0x2100); // wall-hidden
    ram[0x1014]=1;ram[0x2004]&=~0x20;CHECK(DoomAimSelect(ram,sizeof(ram)).object==0x2100); // dead
    ram[0x2004]=0xe0;word(0x100a,124);CHECK(DoomAimSelect(ram,sizeof(ram)).object==0x2100);
    word(0x100a,100);word(0x100c,45);CHECK(DoomAimSelect(ram,sizeof(ram)).object==0x2100);
    word(0x100c,46);CHECK(DoomAimSelect(ram,sizeof(ram)).object==0x2000);
    ram[0x2005]=40;ram[0x2004]=0xa0; // nearest targetable barrel blocks a hitscan lock
    CHECK(!DoomAimSelect(ram,sizeof(ram)).object);
    word(0x1dc,10);CHECK(DoomAimSelect(ram,sizeof(ram)).object==0x2100); // projectiles skip fixed targets
    word(0x1dc,0);CHECK(!DoomAimSelect(ram,sizeof(ram)).object); // melee distance
    ram[0x2005]=6;ram[0x2004]=0xe0;word(0x1000,63);word(0x30,21);
    CHECK(DoomAimSelect(ram,sizeof(ram)).object==0x2000);
    word(0x1008,(unsigned)-21);CHECK(!DoomAimSelect(ram,sizeof(ram)).object); // exact vertical cutoff
    word(0x1008,(unsigned)-20);word(0x1000,64);CHECK(!DoomAimSelect(ram,sizeof(ram)).object);
    word(0x1dc,2);word(0x1000,150);CHECK(DoomAimSelect(ram,sizeof(ram)).object==0x2000); // first wins equal distance
    puts("Crosshair: native target flags, visibility, range, nearest/ties, barrels and read-only selection passed");
}

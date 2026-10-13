#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned cases,scans,item_gates,item_dispatches,clip_calls;
static void Observe(SuperFx *fx,uint32_t pc,void *context) {
    (void)fx;(void)context;CHECK(pc==0xa47a);scans++;
}
static void ObserveItem(SuperFx *fx,uint32_t pc,void *context) {
    (void)fx;(void)context;
    if(pc==0xabf3)item_gates++;
    else if(pc==0xac02)item_dispatches++;
    else { CHECK(pc==0xb230);clip_calls++; }
}
static void Put(uint8_t *data,unsigned address,uint16_t value) {
    data[address]=(uint8_t)value;data[address+1]=(uint8_t)(value>>8);
}
static uint16_t Multiply(uint16_t a,uint16_t b) {
    return (uint16_t)((uint32_t)((int16_t)a*(int16_t)b)>>15);
}
static void Case(SuperFx *fx,uint8_t *expected,unsigned mover,int fixed,unsigned velocity,int vy,int eligible) {
    superfx_reset(fx);scans=item_gates=item_dispatches=clip_calls=0;memset(expected,0,65536);
    int item=eligible>=2,collides=eligible && !item;
    unsigned start=mover==0 ? 0xa42f : mover==1 ? 0xa6d0 : 0xa8c9;
    Put(expected,0x11c,0x5a8e);Put(expected,0x11a,0xf800);Put(expected,0x150,(uint16_t)start);
    expected[0x152]=(uint8_t)(item ? 0x10 : eligible ? 2 : 0);Put(expected,0x124,32);expected[0x98]=0x4f;
    if(item) { expected[0x122]=0x80;Put(expected,0x5e,200);Put(expected,0x2c,(uint16_t)(eligible==3)); }
    Put(expected,0xd80,0x3082);Put(expected,0xd82,0xff80);
    Put(expected,0x7c,0x8000);expected[0x7e]=0x4f;Put(expected,0x82,0x1000);
    Put(expected,0x3082,0);Put(expected,0x3084,128);Put(expected,0x308c,0x5a8e);
    Put(expected,0x309a,0x5b00);expected[0x5b16]=1;
    Put(expected,0x5a8e,0x5b26);expected[0x5a92]=0x42;
    Put(expected,0x5a96,1013);Put(expected,0x5a9e,(uint16_t)(velocity*256));Put(expected,0x5aa0,(uint16_t)vy);
    Put(expected,0x5b28,0x5a8e);expected[0x5b2a]=(uint8_t)((fixed ? 0 : 0x40)|(item ? 0x10 : 2));expected[0x5b2b]=6;
    if(fixed) { Put(expected,0x5b2c,0x9200);Put(fx->rom,0xf9200,1050);Put(fx->rom,0xf9202,0); }
    else Put(expected,0x5b2e,1050);
    if(mover==2) { Put(expected,8,256);Put(expected,0x5a9c,32); }
    memcpy(fx->ram,expected,65536);
    /* The native angle is 0 for this relative placement. Original soldier
     * diameter 40 yields center X=1050-20 and endpoints Y=+/-20. */
    CHECK(fx->rom[0x1bfeba]==40);
    if(eligible) {
        Put(expected,0x7180,0x5b26);Put(expected,0x7182,1030);Put(expected,0x7184,20);
        Put(expected,0x7186,0x8001);Put(expected,0x7188,0);Put(expected,0x718a,(uint16_t)(item ? 0x8000 : 0x8001));
        Put(expected,0x718c,1030);Put(expected,0x718e,(uint16_t)-20);Put(expected,0x7190,0x308c);
        Put(expected,0x7196,0xffff);
    } else Put(expected,0x7180,0xffff);
    if(mover==0 && collides) {
        uint16_t magnitude=(uint16_t)(0-Multiply((uint16_t)vy,0x7fff));
        uint16_t projected=Multiply(0x8001,magnitude),applied=Multiply(projected,0x7fff);
        uint32_t ny=(uint32_t)((int16_t)applied*256);
        Put(expected,0x5a98,(uint16_t)ny);Put(expected,0x5a9a,(uint16_t)(ny>>16));
        Put(expected,0x5a9e,0);Put(expected,0x5aa0,projected);Put(expected,0x5ab2,128);
    }
    if(!collides) {
        uint16_t ax=Multiply((uint16_t)(velocity*256),0x7fff),ay=Multiply((uint16_t)vy,0x7fff);
        uint32_t nx=(1013u<<16)+(uint32_t)((int16_t)ax*256),ny=(uint32_t)((int16_t)ay*256);
        Put(expected,0x5a94,(uint16_t)nx);Put(expected,0x5a96,(uint16_t)(nx>>16));
        Put(expected,0x5a98,(uint16_t)ny);Put(expected,0x5a9a,(uint16_t)(ny>>16));
        if(mover==0)Put(expected,0x5ab2,128);
    }
    if(mover==0 || !collides) {
        expected[0x5aa4]=1;Put(expected,0x308c,0x5b26);Put(expected,0x5b28,0);
        Put(expected,0x309a,0x5a8e);Put(expected,0x5a8e,0x5b00);Put(expected,0x5b02,0x5a8e);
    }
    superfx_set_reg(fx,8,0x7180);
    superfx_cpu_write_io(fx,0x301e,0x89);superfx_cpu_write_io(fx,0x301f,0xa2);
    superfx_sync(fx,fx->master_clock+200000);CHECK(!superfx_is_running(fx));
    for(unsigned a=0;a<65536;a++) {
        if(a>=0x11a && a<0x154)continue;
        if(fx->ram[a]!=expected[a]) {
            fprintf(stderr,"object contact mover=%u fixed=%d velocity=%u vy=%d RAM[%04x] expected=%02x actual=%02x\n",mover,fixed,velocity,vy,a,expected[a],fx->ram[a]);exit(1);
        }
    }
    CHECK(scans==(mover==0 ? (collides ? 2u : 1u) : 0u));CHECK(superfx_reg(fx,12)==0x5a8e);CHECK(superfx_reg(fx,0)==0);
    CHECK((fx->ram[0x13e]|((uint16_t)fx->ram[0x13f]<<8))==((mover==0 || !collides) ? 0x7fff : 0));cases++;
    /* Trigger geometry uses integer coordinates and strict side changes.
     * Velocity 18 ends exactly on X=1030; 19..24 actually cross the plane. */
    CHECK(item_gates==((item && velocity>18) ? 1u : 0u));
    CHECK(item_dispatches==((eligible==2 && velocity>18) ? 1u : 0u));CHECK(clip_calls==item_dispatches);
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t entry[]={0xf0,0x98,0,0x3d,0x40,0x3f,0xdf,0x3d,0xab,0x8e};CHECK(!memcmp(rom+0x2289,entry,sizeof(entry)));
    rom[0x7800]=0;rom[0x7801]=1;memset(rom+0xf8000,0,28);Put(rom,0xf8002,0x7fff);
    Put(rom,0xf8010,0x8000);Put(rom,0xf801a,0x8000);rom[0xf9003]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);CHECK(superfx_set_pc_hook(fx,0xa47a,Observe,NULL));
    CHECK(superfx_set_pc_hook(fx,0xabf3,ObserveItem,NULL));
    CHECK(superfx_set_pc_hook(fx,0xac02,ObserveItem,NULL));CHECK(superfx_set_pc_hook(fx,0xb230,ObserveItem,NULL));
    for(unsigned mover=0;mover<3;mover++)for(int fixed=0;fixed<2;fixed++)for(unsigned v=2;v<=8;v++)for(int vy=-512;vy<=512;vy+=256)for(int eligible=0;eligible<2;eligible++)
        Case(fx,expected,mover,fixed,v,vy,eligible);
    printf("Retail native object scan/geometry/collision response: %u source-oracle cases passed\n",cases);
    unsigned previous=cases;
    for(int fixed=0;fixed<2;fixed++)for(unsigned v=18;v<=24;v++)for(int mode=2;mode<=3;mode++)
        Case(fx,expected,0,fixed,v,0,mode);
    printf("Retail item contact/full-ammo or dead-player refusal: %u source-oracle cases passed\n",cases-previous);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}

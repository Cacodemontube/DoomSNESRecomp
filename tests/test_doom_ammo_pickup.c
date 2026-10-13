#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned rejected,cases;
static void Reject(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==0xab08);CHECK(!rejected);rejected=1;superfx_hook_redirect(fx,0xf800);
}
static void Case(SuperFx *fx,uint8_t *expected,uint16_t ammo,unsigned backpack,unsigned box) {
    superfx_reset(fx);rejected=0;memset(expected,0xa5,65536);
    expected[0x47]=(uint8_t)backpack;expected[0x5e]=(uint8_t)ammo;expected[0x5f]=(uint8_t)(ammo>>8);
    memcpy(fx->ram,expected,65536);
    unsigned capacity=backpack ? 400 : 200,amount=box ? 50 : 5;
    uint16_t difference=(uint16_t)(ammo-capacity);
    int refuse=(int16_t)difference>=0;
    uint16_t next=ammo;
    if(!refuse) {
        next=(uint16_t)(ammo+amount);
        uint16_t remainder=(uint16_t)(difference+amount);
        if((int16_t)remainder>=0)next=(uint16_t)(next-remainder);
    }
    expected[0x5e]=(uint8_t)next;expected[0x5f]=(uint8_t)(next>>8);
    superfx_set_reg(fx,11,0xf800);
    unsigned pc=box ? 0xb235 : 0xb230;
    superfx_cpu_write_io(fx,0x301e,(uint8_t)pc);superfx_cpu_write_io(fx,0x301f,(uint8_t)(pc>>8));
    superfx_sync(fx,fx->master_clock+10000);CHECK(!superfx_is_running(fx));
    if(rejected!=(unsigned)refuse || memcmp(fx->ram,expected,65536)) {
        fprintf(stderr,"ammo=%04x backpack=%u box=%u expected=%04x reject=%d actual=%04x reject=%u\n",
            ammo,backpack,box,next,refuse,fx->ram[0x5e]|((uint16_t)fx->ram[0x5f]<<8),rejected);exit(1);
    }
    cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t code[]={0xa4,0x05,0x05,0x03,0x01,0xa4,0x32,0xfc,0x08,0xab,0xa0,0x47,0x3d,0x40,
        0xf1,0xc8,0x00,0xe0,0x0b,0x02,0x21,0x51,0x3d,0xa0,0x2f,0x11,0x61,0x0a,0x53,0x54,0x21,0x54,
        0x0b,0x02,0x20,0x61,0x9b,0x90};
    CHECK(!memcmp(rom+0x3230,code,sizeof(code)));rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    CHECK(superfx_set_pc_hook(fx,0xab08,Reject,NULL));
    for(unsigned ammo=0;ammo<65536;ammo++)for(unsigned backpack=0;backpack<2;backpack++)for(unsigned box=0;box<2;box++)
        Case(fx,expected,(uint16_t)ammo,backpack,box);
    printf("Retail clip/ammo-box inventory updates: %u source-oracle cases passed\n",cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}

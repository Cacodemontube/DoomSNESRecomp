#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned reached,cases;
static void Done(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==0xa721 || pc==0xa7b5);CHECK(!reached);reached=pc;
    superfx_hook_redirect(fx,0xf800);
}
static void Put(uint8_t *data,unsigned address,uint16_t value) {
    data[address]=(uint8_t)value;data[address+1]=(uint8_t)(value>>8);
}
static int Blocked(uint16_t z,uint16_t high,uint16_t ceiling,uint16_t low) {
    return (int16_t)(uint16_t)(high-z)>=25 ||
        (int16_t)ceiling<(int16_t)(uint16_t)(z+56) ||
        (int16_t)(uint16_t)(ceiling-high)<56 ||
        (int16_t)(uint16_t)(high-low)>=25;
}
static void Case(SuperFx *fx,uint8_t *expected,uint16_t z,uint16_t high,uint16_t ceiling,uint16_t low) {
    superfx_reset(fx);reached=0;memset(expected,0xa5,65536);
    Put(expected,0x144,z);Put(expected,0x7190,high);Put(expected,0x7192,ceiling);Put(expected,0x7194,low);
    memcpy(fx->ram,expected,65536);superfx_set_reg(fx,2,0x7190);
    superfx_cpu_write_io(fx,0x301e,0x8d);superfx_cpu_write_io(fx,0x301f,0xa7);
    superfx_sync(fx,fx->master_clock+20000);CHECK(!superfx_is_running(fx));
    unsigned want=Blocked(z,high,ceiling,low) ? 0xa7b5 : 0xa721;
    if(reached!=want) {
        fprintf(stderr,"enemy z=%04x high=%04x ceiling=%04x low=%04x expected PC=%04x actual=%04x\n",z,high,ceiling,low,want,reached);exit(1);
    }
    CHECK(!memcmp(fx->ram,expected,65536));cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t code[]={0x3d,0xa1,0xa2,0x14,0x42,0xb4,0x61,0xa6,0x19,0x66,0x06,0x1b,0xd2,0xd2,
        0x42,0xab,0x38,0x21,0x5b,0x3f,0x61,0x07,0x10,0x64,0x6b,0x07,0x0c,0xd2,0xd2,0x42,
        0xb4,0x60,0x66,0x06,0x04,0x01,0xff,0x21,0xa7,0x3d};
    CHECK(!memcmp(rom+0x278d,code,sizeof(code)));rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    CHECK(superfx_set_pc_hook(fx,0xa721,Done,NULL));CHECK(superfx_set_pc_hook(fx,0xa7b5,Done,NULL));
    for(unsigned z=0;z<65536;z++) {
        Case(fx,expected,(uint16_t)z,(uint16_t)(z+24),(uint16_t)(z+80),(uint16_t)z);
        Case(fx,expected,(uint16_t)z,(uint16_t)(z+25),(uint16_t)(z+81),(uint16_t)z);
        Case(fx,expected,(uint16_t)z,(uint16_t)z,(uint16_t)(z+55),(uint16_t)z);
        Case(fx,expected,(uint16_t)z,(uint16_t)z,(uint16_t)(z+56),(uint16_t)z);
        Case(fx,expected,(uint16_t)z,(uint16_t)z,(uint16_t)(z+128),(uint16_t)(z-24));
        Case(fx,expected,(uint16_t)z,(uint16_t)z,(uint16_t)(z+128),(uint16_t)(z-25));
    }
    const uint16_t edges[]={0,24,25,56,0x7fff,0x8000,0xffff};
    for(unsigned z=0;z<7;z++)for(unsigned h=0;h<7;h++)for(unsigned c=0;c<7;c++)for(unsigned l=0;l<7;l++)
        Case(fx,expected,edges[z],edges[h],edges[c],edges[l]);
    printf("Retail enemy step/ceiling/cliff decisions: %u source-oracle cases passed\n",cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}

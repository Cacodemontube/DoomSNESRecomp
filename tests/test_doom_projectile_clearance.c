#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned reached,cases;
static void Done(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==0xa931 || pc==0xa9b7);CHECK(!reached);reached=pc;
    superfx_hook_redirect(fx,0xf800);
}
static void Put(uint8_t *data,unsigned address,uint16_t value) {
    data[address]=(uint8_t)value;data[address+1]=(uint8_t)(value>>8);
}
static void Case(SuperFx *fx,uint8_t *expected,uint16_t z,uint16_t floor,uint16_t ceiling) {
    superfx_reset(fx);reached=0;memset(expected,0xa5,65536);
    Put(expected,0x144,z);Put(expected,0x7190,floor);Put(expected,0x7192,ceiling);
    memcpy(fx->ram,expected,65536);superfx_set_reg(fx,2,0x7190);
    int block=(int16_t)floor>=(int16_t)z ||
        (int16_t)ceiling<(int16_t)(uint16_t)(z+16) || (int16_t)(uint16_t)(ceiling-floor)<16;
    superfx_cpu_write_io(fx,0x301e,0x99);superfx_cpu_write_io(fx,0x301f,0xa9);
    superfx_sync(fx,fx->master_clock+20000);CHECK(!superfx_is_running(fx));
    unsigned want=block ? 0xa9b7 : 0xa931;
    if(reached!=want) {
        fprintf(stderr,"projectile z=%04x floor=%04x ceiling=%04x expected PC=%04x actual=%04x\n",z,floor,ceiling,want,reached);exit(1);
    }
    CHECK(!memcmp(fx->ram,expected,65536));cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t code[]={0x3d,0xa1,0xa2,0x42,0x3f,0x61,0x06,0x15,0xd2,0xd2,0x20,0x14,0x42,
        0xa6,0x10,0x21,0x56,0x3f,0x61,0x07,0x08,0x64,0x66,0x07,0x04,0x01,0xff,0x31,0xa9,0x3d};
    CHECK(!memcmp(rom+0x2999,code,sizeof(code)));rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    CHECK(superfx_set_pc_hook(fx,0xa931,Done,NULL));CHECK(superfx_set_pc_hook(fx,0xa9b7,Done,NULL));
    for(unsigned z=0;z<65536;z++) {
        Case(fx,expected,(uint16_t)z,(uint16_t)(z-1),(uint16_t)(z+16));
        Case(fx,expected,(uint16_t)z,(uint16_t)z,(uint16_t)(z+16));
        Case(fx,expected,(uint16_t)z,(uint16_t)(z+1),(uint16_t)(z+17));
        Case(fx,expected,(uint16_t)z,(uint16_t)(z-1),(uint16_t)(z+15));
        Case(fx,expected,(uint16_t)z,(uint16_t)(z-16),(uint16_t)(z+16));
        Case(fx,expected,(uint16_t)z,(uint16_t)(z-17),(uint16_t)(z+17));
    }
    const uint16_t edges[]={0,1,15,16,0x7fff,0x8000,0xffff};
    for(unsigned z=0;z<7;z++)for(unsigned f=0;f<7;f++)for(unsigned c=0;c<7;c++)Case(fx,expected,edges[z],edges[f],edges[c]);
    printf("Retail projectile floor/ceiling decisions: %u source-oracle cases passed\n",cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}

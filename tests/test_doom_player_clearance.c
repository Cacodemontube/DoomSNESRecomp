#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned reached,cases;
static uint16_t status;
static void Done(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==0xa535);CHECK(!reached);reached=1;
    status=superfx_reg(fx,0);superfx_hook_redirect(fx,0xf800);
}
static void Put(uint8_t *ram,unsigned address,uint16_t value) {
    ram[address]=(uint8_t)value;ram[address+1]=(uint8_t)(value>>8);
}
/* Source comparisons include 16-bit intermediate wrap. The original allows
 * at most a 24-unit upward step and requires the standing height of 56. */
static uint16_t Oracle(uint16_t z,uint16_t floor,uint16_t ceiling) {
    if((int16_t)(uint16_t)(floor-z)>=25)return 0;
    if((int16_t)ceiling<(int16_t)(uint16_t)(z+56))return 0;
    if((int16_t)(uint16_t)(ceiling-floor)<56)return 0;
    return (uint16_t)(floor*2+1);
}
static void Case(SuperFx *fx,uint8_t *expected,uint16_t z,uint16_t floor,uint16_t ceiling) {
    superfx_reset(fx);reached=0;memset(expected,0xa5,65536);
    Put(expected,0x144,z);Put(expected,0x7190,floor);Put(expected,0x7192,ceiling);
    memcpy(fx->ram,expected,65536);uint16_t want=Oracle(z,floor,ceiling);Put(expected,0x14c,want);
    superfx_set_reg(fx,2,0x7190);
    superfx_cpu_write_io(fx,0x301e,0x12);superfx_cpu_write_io(fx,0x301f,0xa5);
    superfx_sync(fx,fx->master_clock+20000);CHECK(!superfx_is_running(fx));CHECK(reached);
    if(status!=want || memcmp(fx->ram,expected,65536)) {
        fprintf(stderr,"clearance z=%04x floor=%04x ceiling=%04x expected=%04x actual=%04x\n",z,floor,ceiling,want,status);exit(1);
    }
    cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t code[]={0x3d,0xa1,0xa2,0x14,0x42,0xb4,0x61,0xa6,0x19,0x66,0x06,0x13,0xd2,0xd2,
        0x42,0xa6,0x38,0x21,0x56,0x3f,0x61,0x07,0x08,0x64,0x66,0xb4,0x07,0x03,0x54,0xd0,
        0x21,0x60,0x3e,0xa0,0xa6};
    CHECK(!memcmp(rom+0x2512,code,sizeof(code)));
    rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    CHECK(superfx_set_pc_hook(fx,0xa535,Done,NULL));
    for(unsigned z=0;z<65536;z++) {
        Case(fx,expected,(uint16_t)z,(uint16_t)(z+24),(uint16_t)(z+80));
        Case(fx,expected,(uint16_t)z,(uint16_t)(z+25),(uint16_t)(z+81));
        Case(fx,expected,(uint16_t)z,(uint16_t)z,(uint16_t)(z+55));
        Case(fx,expected,(uint16_t)z,(uint16_t)z,(uint16_t)(z+56));
        Case(fx,expected,(uint16_t)z,(uint16_t)(z+24),(uint16_t)(z+79));
        Case(fx,expected,(uint16_t)z,(uint16_t)(z-1),(uint16_t)(z+56));
    }
    const uint16_t heights[]={0,1,24,25,55,56,0x7fff,0x8000,0xffff};
    for(unsigned z=0;z<9;z++)for(unsigned f=0;f<9;f++)for(unsigned c=0;c<9;c++)
        Case(fx,expected,heights[z],heights[f],heights[c]);
    printf("Retail player step/ceiling clearance: %u source-oracle cases passed\n",cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}

#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
/* rl mobjects3: constant 16 * signed 8.8 FPSRatio; extract bits 8..23.
 * SUB wraps to 16 bits, CMP/BGE compares the wrapped value signed, and the
 * MOVES in the branch delay slot clamps only on the untaken branch. */
static uint16_t Oracle(uint16_t ratio,uint16_t z,uint16_t floor) {
    int32_t product=(int16_t)ratio * 16;
    uint16_t decrement=(uint16_t)((uint32_t)product >> 8);
    uint16_t next=(uint16_t)(z-decrement);
    return (int16_t)next >= (int16_t)floor ? next : floor;
}
static unsigned cases;
static void Case(SuperFx *fx,uint16_t ratio,uint16_t z,uint16_t floor) {
    superfx_reset(fx);
    memset(fx->ram,0xa5,fx->ram_size);
    superfx_set_reg(fx,1,z);superfx_set_reg(fx,2,floor);
    superfx_set_reg(fx,3,ratio);superfx_set_reg(fx,11,0xf800);
    fx->ramaddr=0x5a9c;
    superfx_cpu_write_io(fx,0x301e,0xbd);
    superfx_cpu_write_io(fx,0x301f,0x92);
    superfx_sync(fx,fx->master_clock+10000);
    CHECK(!superfx_is_running(fx));
    uint16_t actual=superfx_ram_peek(fx,0x5a9c) | ((uint16_t)superfx_ram_peek(fx,0x5a9d)<<8);
    uint16_t expected=Oracle(ratio,z,floor);
    if(actual!=expected) {
        fprintf(stderr,"ratio=%04x z=%04x floor=%04x expected=%04x actual=%04x\n",ratio,z,floor,expected,actual);
        exit(1);
    }
    CHECK(superfx_reg(fx,0)==expected);
    CHECK(superfx_reg(fx,6)==(uint16_t)((uint32_t)((int16_t)ratio*16)>>8));
    for(unsigned i=0;i<fx->ram_size;i++)
        if(i!=0x5a9c && i!=0x5a9d)CHECK(fx->ram[i]==0xa5);
    cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);
    uint8_t *rom=calloc(0x200000,1),*ram=calloc(0x10000,1);CHECK(rom && ram);
    FILE *f=fopen(argv[1],"rb");CHECK(f);
    CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t gravity[]={0xa6,0x10,0xb3,0x3d,0x9f,0x9e,0x4d,0x24,0xc0,0x16,0xc4,
        0xb1,0x66,0x3f,0x62,0x06,0x02,0x20,0xb2,0x90,0x9b,0x01};
    CHECK(!memcmp(rom+0x12bd,gravity,sizeof(gravity)));
    /* Only the return destination is a synthetic STOP; all tested arithmetic,
     * branches, delay slots and SBK bytes are unmodified retail instructions. */
    rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,0x10000);CHECK(fx);
    for(unsigned ratio=0;ratio<65536;ratio++)Case(fx,(uint16_t)ratio,1000,0);
    const uint16_t ratios[]={0,1,15,16,255,256,257,511,512,1024,0x7fff,0x8000,0xffff};
    const uint16_t heights[]={0,1,15,16,17,56,1000,0x7fff,0x8000,0xffff};
    for(unsigned r=0;r<sizeof(ratios)/sizeof(*ratios);r++)
        for(unsigned z=0;z<sizeof(heights)/sizeof(*heights);z++)
            for(unsigned h=0;h<sizeof(heights)/sizeof(*heights);h++)Case(fx,ratios[r],heights[z],heights[h]);
    printf("Retail gravity arithmetic: %u source-oracle cases passed\n",cases);
    superfx_destroy(fx);free(rom);free(ram);return 0;
}

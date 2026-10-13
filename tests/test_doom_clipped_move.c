#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned reached,cases;
static void Done(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==0xa64a);CHECK(!reached);reached=1;
    superfx_hook_redirect(fx,0xf800);
}
static void Put(uint8_t *data,unsigned address,uint16_t value) {
    data[address]=(uint8_t)value;data[address+1]=(uint8_t)(value>>8);
}
static uint16_t Applied(uint16_t velocity,uint16_t percent) {
    /* FMULT followed by ROL extracts product bits 15..30, including the
     * carry bit from multiplication. Preserve signed 16-bit wrapping. */
    uint32_t product=(uint32_t)((int16_t)velocity*(int16_t)percent);
    return (uint16_t)(product>>15);
}
static uint32_t Coordinate(uint32_t coordinate,uint16_t applied) {
    return coordinate+(uint32_t)((int16_t)applied*256);
}
static void Case(SuperFx *fx,uint8_t *expected,uint16_t percent,uint16_t vx,uint16_t vy,uint32_t x,uint32_t y) {
    superfx_reset(fx);reached=0;memset(expected,0xa5,65536);
    Put(expected,0x13e,percent);Put(expected,0x12c,vx);Put(expected,0x12e,vy);Put(expected,0x11c,0x5a8e);
    Put(expected,0x5a94,(uint16_t)x);Put(expected,0x5a96,(uint16_t)(x>>16));
    Put(expected,0x5a98,(uint16_t)y);Put(expected,0x5a9a,(uint16_t)(y>>16));
    memcpy(fx->ram,expected,65536);
    uint16_t ax=Applied(vx,percent),ay=Applied(vy,percent);
    uint32_t nx=Coordinate(x,ax),ny=Coordinate(y,ay);
    Put(expected,0x5a94,(uint16_t)nx);Put(expected,0x5a96,(uint16_t)(nx>>16));
    Put(expected,0x5a98,(uint16_t)ny);Put(expected,0x5a9a,(uint16_t)(ny>>16));
    superfx_cpu_write_io(fx,0x301e,7);superfx_cpu_write_io(fx,0x301f,0xa6);
    superfx_sync(fx,fx->master_clock+20000);CHECK(!superfx_is_running(fx));CHECK(reached);
    if(superfx_reg(fx,9)!=(uint16_t)(vx-ax) || superfx_reg(fx,10)!=(uint16_t)(vy-ay) || memcmp(fx->ram,expected,65536)) {
        fprintf(stderr,"percent=%04x v=%04x,%04x coordinates=%08x,%08x expected=%08x,%08x remainder=%04x,%04x actual=%04x,%04x\n",
            percent,vx,vy,x,y,nx,ny,(uint16_t)(vx-ax),(uint16_t)(vy-ay),superfx_reg(fx,9),superfx_reg(fx,10));exit(1);
    }
    CHECK(superfx_reg(fx,7)==(uint16_t)(nx>>16) && superfx_reg(fx,8)==(uint16_t)(ny>>16));
    CHECK(superfx_reg(fx,11)==0x5a9a && superfx_reg(fx,6)==percent);cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t code[]={0x3d,0xa6,0x9f,0x3d,0xa9,0x96,0x3d,0xaa,0x97,0x3d,0xab,0x8e,0x2b,0x3e,0x56,
        0xb9,0x9f,0x04,0x29,0x60,0x11,0x9e,0x21,0x4d,0xc0,0x13,0x95,0x4b,0x21,0x50,0xb1,0x90,
        0xdb,0xdb,0x4b,0x17,0x3d,0x53,0xb7,0x90,0xdb,0xdb,0xba,0x9f,0x04,0x2a,0x60,0x12,0x9e,0x22,
        0x4d,0xc0,0x13,0x95,0x4b,0x22,0x50,0xb2,0x90,0xdb,0xdb,0x4b,0x18,0x3d,0x53,0xb8,0x90};
    CHECK(!memcmp(rom+0x2607,code,sizeof(code)));rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    CHECK(superfx_set_pc_hook(fx,0xa64a,Done,NULL));
    for(unsigned value=0;value<65536;value++) {
        Case(fx,expected,(uint16_t)value,513,0xfdff,0x7fffffff,0x80000001);
        Case(fx,expected,(uint16_t)value,0x7fff,0x8000,0xffffffff,0);
        Case(fx,expected,0x7fff,(uint16_t)value,(uint16_t)~value,0xffff0001,0x0000ffff);
        Case(fx,expected,0x4000,(uint16_t)value,(uint16_t)~value,0x8000ff00,0x7fff00ff);
        Case(fx,expected,0,(uint16_t)value,(uint16_t)~value,0x12345678,0x9abcdef0);
    }
    printf("Retail collision-clipped coordinate update: %u source-oracle cases passed\n",cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}

#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned cases;
/* rlmove4.a _RM4XY4000: signed dot product with 32-bit modular addition.
 * R2 retains the signed high word. Negative high words in R1 are incremented
 * even for exact integral products; preserve this native rounding rule. */
static void Case(SuperFx *fx,uint8_t *expected,uint16_t x,uint16_t y,uint16_t px,uint16_t py) {
    superfx_reset(fx);memset(expected,0xa5,65536);memcpy(fx->ram,expected,65536);
    superfx_set_reg(fx,0,x);superfx_set_reg(fx,1,y);
    superfx_set_reg(fx,3,px);superfx_set_reg(fx,5,py);superfx_set_reg(fx,11,0xf800);
    uint32_t productx=(uint32_t)((int16_t)x*(int16_t)px);
    uint32_t producty=(uint32_t)((int16_t)y*(int16_t)py);
    uint16_t high=(uint16_t)((productx+producty)>>16);
    uint16_t rounded=(uint16_t)(high+((int16_t)high<0));
    superfx_cpu_write_io(fx,0x301e,0xf0);superfx_cpu_write_io(fx,0x301f,0xa5);
    superfx_sync(fx,fx->master_clock+10000);CHECK(!superfx_is_running(fx));
    if(superfx_reg(fx,1)!=rounded || superfx_reg(fx,2)!=high) {
        fprintf(stderr,"x=%04x y=%04x perpendicular=%04x,%04x expected=%04x,%04x actual=%04x,%04x\n",
            x,y,px,py,rounded,high,superfx_reg(fx,1),superfx_reg(fx,2));exit(1);
    }
    CHECK(superfx_reg(fx,0)==(uint16_t)(productx>>16));
    CHECK(superfx_reg(fx,4)==(uint16_t)productx && superfx_reg(fx,6)==px);
    CHECK(!memcmp(fx->ram,expected,65536));cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t code[]={0x25,0x16,0x21,0x3d,0x9f,0x24,0x12,0x23,0x16,0x3d,0x9f,0x22,0x54,
        0x21,0x3d,0x50,0x21,0x0a,0x02,0x12,0xd1,0x9b,0x01};
    CHECK(!memcmp(rom+0x25f0,code,sizeof(code)));rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    const uint16_t vector[][2]={{0x7fff,0},{0,0x7fff},{0x8000,0},{0,0x8000},{0x5a82,0x5a82},{0x5a82,0xa57e}};
    for(unsigned value=0;value<65536;value++)for(unsigned v=0;v<6;v++)
        Case(fx,expected,(uint16_t)value,(uint16_t)~value,vector[v][0],vector[v][1]);
    const uint16_t edges[]={0,1,0x00ff,0x0100,0x7fff,0x8000,0xffff};
    for(unsigned x=0;x<7;x++)for(unsigned y=0;y<7;y++)for(unsigned px=0;px<7;px++)for(unsigned py=0;py<7;py++)
        Case(fx,expected,edges[x],edges[y],edges[px],edges[py]);
    printf("Retail player line-side dot product: %u source-oracle cases passed\n",cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}

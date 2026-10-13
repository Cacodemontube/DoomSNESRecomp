#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned reached,cases,stop_pc=0xa01d;
static void Done(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==stop_pc);CHECK(!reached);reached=1;
    superfx_hook_redirect(fx,0xf800);
}
static void Put(uint8_t *ram,unsigned address,uint16_t value) {
    ram[address]=(uint8_t)value;ram[address+1]=(uint8_t)(value>>8);
}
/* rlmove.a: a 16.16 coordinate accumulates signed 8.8 velocity.
 * Unsigned modular arithmetic also models carry into a wrapped integer. */
static uint32_t Next(uint16_t whole,uint16_t fraction,uint16_t velocity) {
    uint32_t coordinate=((uint32_t)whole<<16)|fraction;
    int32_t increment=(int16_t)velocity*256;
    return coordinate+(uint32_t)increment;
}
static void Case(SuperFx *fx,uint8_t *expected,uint16_t x,uint16_t xf,uint16_t y,uint16_t yf,uint16_t vx,uint16_t vy) {
    superfx_reset(fx);reached=0;
    memset(expected,0xa5,65536);memcpy(fx->ram,expected,65536);
    superfx_set_reg(fx,1,xf);superfx_set_reg(fx,2,yf);
    superfx_set_reg(fx,7,x);superfx_set_reg(fx,8,y);
    superfx_set_reg(fx,9,vx);superfx_set_reg(fx,10,vy);
    superfx_set_reg(fx,12,0x5ab4);
    Put(expected,0x11c,0x5ab4);
    superfx_cpu_write_io(fx,0x301e,0);superfx_cpu_write_io(fx,0x301f,0xa0);
    superfx_sync(fx,fx->master_clock+10000);
    CHECK(!superfx_is_running(fx));CHECK(reached);
    uint32_t nx=Next(x,xf,vx),ny=Next(y,yf,vy);
    if(superfx_reg(fx,1)!=(uint16_t)nx || superfx_reg(fx,2)!=(uint16_t)ny ||
       superfx_reg(fx,9)!=(uint16_t)(nx>>16) || superfx_reg(fx,10)!=(uint16_t)(ny>>16)) {
        fprintf(stderr,"x=%04x:%04x y=%04x:%04x vx=%04x vy=%04x expected=%08x,%08x actual=%04x:%04x,%04x:%04x\n",
            x,xf,y,yf,vx,vy,nx,ny,superfx_reg(fx,9),superfx_reg(fx,1),superfx_reg(fx,10),superfx_reg(fx,2));exit(1);
    }
    CHECK(superfx_reg(fx,5)==(uint16_t)(int16_t)(int8_t)(vx>>8));
    CHECK(superfx_reg(fx,6)==(uint16_t)(int16_t)(int8_t)(vy>>8));
    CHECK(superfx_reg(fx,7)==x && superfx_reg(fx,8)==y);
    CHECK(!memcmp(fx->ram,expected,65536));cases++;
}
static unsigned block_cases;
static uint16_t expected_bounds[4];
static unsigned bounds_seen;
static void CheckBounds(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==0xa095);CHECK(!bounds_seen);bounds_seen=1;
    CHECK(superfx_reg(fx,1)==expected_bounds[0]);
    CHECK(superfx_reg(fx,3)==expected_bounds[1]);
    CHECK(superfx_reg(fx,2)==expected_bounds[2]);
    CHECK(superfx_reg(fx,4)==expected_bounds[3]);
}
static uint16_t Block(uint16_t coordinate,uint16_t origin,unsigned maximum) {
    uint16_t relative=(uint16_t)(coordinate-origin);
    if(relative&0x8000)relative=0;
    unsigned block=((uint16_t)(relative*2))>>8;
    return (uint16_t)(block>maximum ? maximum : block);
}
static void BlockCase(SuperFx *fx,uint8_t *expected,uint16_t x,uint16_t xf,uint16_t y,uint16_t yf,
                      uint16_t vx,uint16_t vy,uint16_t ox,uint16_t oy,unsigned diameter,unsigned width,unsigned height) {
    superfx_reset(fx);reached=0;bounds_seen=0;memset(expected,0xa5,65536);
    Put(expected,0x124,(uint16_t)diameter);Put(expected,0xaa,ox);Put(expected,0xac,oy);
    Put(expected,0xae,(uint16_t)((height<<8)|width));memcpy(fx->ram,expected,65536);
    superfx_set_reg(fx,1,xf);superfx_set_reg(fx,2,yf);
    superfx_set_reg(fx,7,x);superfx_set_reg(fx,8,y);
    superfx_set_reg(fx,9,vx);superfx_set_reg(fx,10,vy);
    superfx_set_reg(fx,12,0x5ab4);superfx_set_reg(fx,11,0x1234);
    Put(expected,0x11c,0x5ab4);Put(expected,0x11a,0x1234);
    unsigned radius=(diameter>>1)+1;
    int rx=(int16_t)vx<0 ? (int)radius : -(int)radius;
    int ry=(int16_t)vy<0 ? (int)radius : -(int)radius;
    uint16_t nx=(uint16_t)(Next(x,xf,vx)>>16),ny=(uint16_t)(Next(y,yf,vy)>>16);
    uint16_t bx0=Block((uint16_t)(x+rx),ox,width-1),bx1=Block((uint16_t)(nx-rx),ox,width-1);
    uint16_t by0=Block((uint16_t)(y+ry),oy,height-1),by1=Block((uint16_t)(ny-ry),oy,height-1);
    unsigned countx=(bx1>bx0 ? bx1-bx0 : bx0-bx1)+1;
    unsigned county=(by1>by0 ? by1-by0 : by0-by1)+1;
    expected_bounds[0]=bx0;expected_bounds[1]=bx1;expected_bounds[2]=by0;expected_bounds[3]=by1;
    superfx_cpu_write_io(fx,0x301e,0);superfx_cpu_write_io(fx,0x301f,0xa0);
    superfx_sync(fx,fx->master_clock+20000);
    CHECK(!superfx_is_running(fx));CHECK(reached);CHECK(bounds_seen);
    if(superfx_reg(fx,1)!=bx0 || superfx_reg(fx,3)!=countx ||
       superfx_reg(fx,2)!=by0 || superfx_reg(fx,4)!=county) {
        fprintf(stderr,"block bounds x=%04x y=%04x v=%04x,%04x origin=%04x,%04x d=%u dims=%u,%u expected=%u,%u,%u,%u actual=%u,%u,%u,%u\n",
            x,y,vx,vy,ox,oy,diameter,width,height,bx0,countx,by0,county,
            superfx_reg(fx,1),superfx_reg(fx,3),superfx_reg(fx,2),superfx_reg(fx,4));exit(1);
    }
    CHECK(superfx_reg(fx,5)==((int16_t)vx<0 ? 0xfffe : 2));
    int stepx=(int16_t)vx<0 ? -2 : 2;
    int row=(int16_t)vy<0 ? -(int)(width*2) : (int)(width*2);
    /* Native MULT uses signed low bytes even for very wide synthetic maps. */
    CHECK(superfx_reg(fx,6)==(uint16_t)(row-(int8_t)countx*stepx));
    CHECK(superfx_reg(fx,11)==radius && superfx_reg(fx,12)==width*2);
    CHECK(superfx_reg(fx,9)==nx && superfx_reg(fx,10)==ny);
    CHECK(!memcmp(fx->ram,expected,65536));block_cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);
    uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t code[]={0xb9,0x9e,0x4d,0x21,0x50,0xb9,0xc0,0x15,0x95,0xb7,0x19,0x3d,0x55,
        0x3e,0xac,0x8e,0xba,0x9e,0x4d,0x22,0x50,0xba,0xc0,0x16,0x95,0xb8,0x1a,0x3d,0x56};
    CHECK(!memcmp(rom+0x2000,code,sizeof(code)));
    rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    CHECK(superfx_set_pc_hook(fx,0xa01d,Done,NULL));
    const uint16_t fractions[]={0,1,255,256,0x7fff,0x8000,0xff00,0xffff};
    for(unsigned v=0;v<65536;v++)for(unsigned i=0;i<8;i++)
        Case(fx,expected,(uint16_t)v,fractions[i],(uint16_t)~v,fractions[7-i],(uint16_t)v,(uint16_t)~v);
    printf("Retail attempted movement coordinates: %u source-oracle cases passed\n",cases);
    CHECK(superfx_set_pc_hook(fx,0xa01d,NULL,NULL));stop_pc=0xa0b4;
    CHECK(superfx_set_pc_hook(fx,stop_pc,Done,NULL));
    CHECK(superfx_set_pc_hook(fx,0xa095,CheckBounds,NULL));
    const uint8_t bounds_entry[]={0x3d,0xa0,0x92,0x3e,0xab,0x8d,0x03,0xd0};
    CHECK(!memcmp(rom+0x201d,bounds_entry,sizeof(bounds_entry)));
    for(unsigned coordinate=0;coordinate<65536;coordinate++) {
        BlockCase(fx,expected,(uint16_t)coordinate,0xffff,(uint16_t)~coordinate,0xff00,1,0xffff,0,0,32,255,255);
        BlockCase(fx,expected,(uint16_t)coordinate,0,(uint16_t)coordinate,0,0xff00,0x0100,0x8000,0x8000,56,16,24);
    }
    const uint16_t edges[]={0,1,127,128,129,0x7fff,0x8000,0xffff};
    for(unsigned a=0;a<8;a++)for(unsigned v=0;v<8;v++)for(unsigned o=0;o<8;o++)
        BlockCase(fx,expected,edges[a],0xffff,edges[7-a],0,edges[v],edges[7-v],edges[o],edges[7-o],0,1,1);
    printf("Retail block-map endpoint bounds: %u source-oracle cases passed\n",block_cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}

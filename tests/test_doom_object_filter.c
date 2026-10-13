#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned accepted,cases;
static uint16_t expected_x,expected_y,expected_dx,expected_dy;
static void Candidate(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==0xa2f6);CHECK(!accepted);accepted++;
    CHECK(superfx_reg(fx,7)==0x5b00);
    CHECK(superfx_reg(fx,9)==expected_x && superfx_reg(fx,10)==expected_y);
    CHECK(superfx_reg(fx,2)==expected_dx && superfx_reg(fx,3)==expected_dy);
    /* Geometry is audited separately; resume the native iterator from this
     * object's linked-list cursor, retaining all filtering instructions. */
    superfx_hook_redirect(fx,0xa2a8);
}
static void Put(uint8_t *bytes,unsigned address,uint16_t value) {
    bytes[address]=(uint8_t)value;bytes[address+1]=(uint8_t)(value>>8);
}
static void Case(SuperFx *fx,uint8_t *expected,uint16_t x,uint16_t y,uint16_t dx,uint16_t dy,int movable,unsigned mask) {
    superfx_reset(fx);accepted=0;memset(expected,0xa5,65536);
    Put(expected,0x98,0x4f);Put(expected,0x11c,0x5a8e);Put(expected,0x150,0xf800);
    Put(expected,0xd80,0x3082);Put(expected,0xd82,0xff80);Put(expected,0x308c,0x5a8e);
    Put(expected,0x5a8e,0x5ac0);expected[0x5a92]=0xff;
    Put(expected,0x5a96,x);Put(expected,0x5a9a,y);
    Put(expected,0x5ac0,0x5b00);expected[0x5ac4]=0;
    Put(expected,0x5b00,0);expected[0x5b04]=(uint8_t)(2|(movable ? 0x40 : 0));
    expected[0x152]=(uint8_t)mask;
    expected_dx=dx;expected_dy=dy;expected_x=(uint16_t)(x+dx);expected_y=(uint16_t)(y+dy);
    if(movable) { Put(expected,0x5b08,expected_x);Put(expected,0x5b0c,expected_y); }
    else {
        Put(expected,0x5b06,0x9000);
        Put(fx->rom,0xf9000,expected_x);Put(fx->rom,0xf9002,expected_y);
    }
    Put(expected,0x7180,0xffff);memcpy(fx->ram,expected,65536);
    superfx_set_reg(fx,8,0x7180);
    superfx_cpu_write_io(fx,0x301e,0x89);superfx_cpu_write_io(fx,0x301f,0xa2);
    superfx_sync(fx,fx->master_clock+20000);CHECK(!superfx_is_running(fx));
    unsigned want=(mask & (2|(movable ? 0x40 : 0))) &&
        (int16_t)dx>=-256 && (int16_t)dx<256 && (int16_t)dy>=-256 && (int16_t)dy<256;
    if(accepted!=want) {
        fprintf(stderr,"x=%04x y=%04x delta=%04x,%04x movable=%d mask=%02x expected=%u actual=%u\n",x,y,dx,dy,movable,mask,want,accepted);exit(1);
    }
    CHECK(superfx_reg(fx,8)==0x7180);
    CHECK(!memcmp(fx->ram,expected,65536));cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t entry[]={0xf0,0x98,0x00,0x3d,0x40,0x3f,0xdf,0x3d,0xab,0x8e};
    const uint8_t range[]={0xb9,0x12,0x6c,0xb2,0x0a,0x03,0xc0,0x95,0xd0,0x08,0xc4,
        0xba,0x13,0x6d,0xb3,0x0a,0x03,0xc0,0x95,0xd0,0x08,0xb9,0x01};
    CHECK(!memcmp(rom+0x2289,entry,sizeof(entry)));CHECK(!memcmp(rom+0x22df,range,sizeof(range)));
    rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    CHECK(superfx_set_pc_hook(fx,0xa2f6,Candidate,NULL));
    for(unsigned delta=0;delta<65536;delta++)for(int movable=0;movable<2;movable++) {
        Case(fx,expected,0x7fff,0xffff,(uint16_t)delta,0,movable,2);
        Case(fx,expected,0x8000,1,0,(uint16_t)delta,movable,2);
    }
    const uint16_t edges[]={0,1,255,256,0xfeff,0xff00,0xff01,0xffff};
    for(unsigned x=0;x<8;x++)for(unsigned y=0;y<8;y++)for(unsigned mask=0;mask<256;mask++)
        Case(fx,expected,0xffff,0x8000,edges[x],edges[y],mask&1,mask);
    printf("Retail object collision filtering: %u source-oracle cases passed\n",cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}

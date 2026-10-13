#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned reached,cases;
static void Destination(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(!reached);reached=pc;
    superfx_hook_redirect(fx,0xf800);
}
static void Put(uint8_t *ram,unsigned address,uint16_t value) {
    ram[address]=(uint8_t)value;ram[address+1]=(uint8_t)(value>>8);
}
/* rlplayer2.a _RLP2000: native multiplayer object-pointer variant.
 * Compare floor height >= player Z using signed 16-bit operands. */
static void Case(SuperFx *fx,uint8_t *expected,uint16_t z,uint16_t floor,unsigned object) {
    superfx_reset(fx);reached=0;
    memset(expected,0xa5,65536);
    Put(expected,0x18c,(uint16_t)object);
    Put(expected,object+14,z);Put(expected,object+34,floor);
    memcpy(fx->ram,expected,65536);
    CHECK(superfx_set_pc_hook(fx,0x81c1,Destination,NULL));
    CHECK(superfx_set_pc_hook(fx,0x8296,Destination,NULL));
    superfx_cpu_write_io(fx,0x301e,0xac);
    superfx_cpu_write_io(fx,0x301f,0x81);
    superfx_sync(fx,fx->master_clock+10000);
    CHECK(!superfx_is_running(fx));
    unsigned target=(int16_t)floor >= (int16_t)z ? 0x81c1 : 0x8296;
    if(reached!=target) {
        fprintf(stderr,"z=%04x floor=%04x expected PC=%04x actual PC=%04x\n",z,floor,target,reached);exit(1);
    }
    CHECK(superfx_reg(fx,2)==object);
    CHECK(superfx_reg(fx,1)==(uint16_t)(floor-z));
    CHECK(superfx_reg(fx,0)==z);
    CHECK(!memcmp(fx->ram,expected,65536));cases++;
}
static unsigned turn_cases;
static void TurnCase(SuperFx *fx,uint8_t *expected,uint16_t mouse,uint16_t ratio,uint16_t angle) {
    superfx_reset(fx);reached=0;
    memset(expected,0xa5,65536);
    Put(expected,0x18c,0x5a8e);Put(expected,0x1f2,mouse);
    Put(expected,8,ratio);Put(expected,0x5aa2,angle);
    memcpy(fx->ram,expected,65536);
    superfx_set_reg(fx,9,0); /* No strafe: reach the native rotation branch. */
    int32_t product=(int16_t)mouse*(int16_t)ratio;
    uint16_t delta=(uint16_t)((uint32_t)product>>8);
    uint16_t next=mouse ? (uint16_t)((angle+delta)&0xfffeu) : angle;
    Put(expected,0x5aa2,next);
    superfx_cpu_write_io(fx,0x301e,0xc1);
    superfx_cpu_write_io(fx,0x301f,0x81);
    superfx_sync(fx,fx->master_clock+10000);
    CHECK(!superfx_is_running(fx));CHECK(reached==0x8246);
    uint16_t actual=superfx_ram_peek(fx,0x5aa2)|((uint16_t)superfx_ram_peek(fx,0x5aa3)<<8);
    if(actual!=next) {
        fprintf(stderr,"mouse=%04x ratio=%04x angle=%04x expected=%04x actual=%04x\n",mouse,ratio,angle,next,actual);exit(1);
    }
    CHECK(!memcmp(fx->ram,expected,65536));turn_cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);
    uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t gate[]={0xa0,0x22,0x3d,0xa2,0xc6,0x52,0x11,0x40,0xa0,0x0e,0x52,0x40,
        0x21,0x60,0x06,0x05,0x01,0xff,0x96,0x82,0x01};
    CHECK(!memcmp(rom+0x1ac,gate,sizeof(gate)));
    const uint8_t turn[]={0x3d,0xa6,0xf9,0x26,0xb6,0x09,0x17,0x01,0x3d,0xa0,0x04,
        0x3d,0x9f,0x9e,0x4d,0x24,0xc0,0x16,0xc4,0xa0,0x14,0x3d,0xa1,0xc6,0x51,
        0x40,0x56,0x3f,0x71,0x90};
    CHECK(!memcmp(rom+0x228,turn,sizeof(turn)));
    rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    for(unsigned z=0;z<65536;z++) {
        Case(fx,expected,(uint16_t)z,0,0x5a8e);
        Case(fx,expected,(uint16_t)z,0x7fff,0x5ab4);
        Case(fx,expected,(uint16_t)z,0x8000,0x5a8e);
    }
    const uint16_t heights[]={0,1,15,16,17,56,1000,0x7fff,0x8000,0xffff};
    for(unsigned z=0;z<10;z++)for(unsigned h=0;h<10;h++)Case(fx,expected,heights[z],heights[h],0x5ab4);
    printf("Retail player falling gate: %u source-oracle cases passed\n",cases);
    CHECK(superfx_set_pc_hook(fx,0x81c1,NULL,NULL));
    CHECK(superfx_set_pc_hook(fx,0x8296,NULL,NULL));
    CHECK(superfx_set_pc_hook(fx,0x8246,Destination,NULL));
    for(unsigned v=0;v<65536;v++) {
        TurnCase(fx,expected,(uint16_t)v,256,0xff01);
        TurnCase(fx,expected,64,(uint16_t)v,0x1235);
    }
    const uint16_t values[]={0,1,255,256,257,0x7fff,0x8000,0xffff};
    for(unsigned m=0;m<8;m++)for(unsigned r=0;r<8;r++)for(unsigned a=0;a<8;a++)
        TurnCase(fx,expected,values[m],values[r],values[a]);
    printf("Retail player rotation: %u source-oracle cases passed\n",turn_cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}

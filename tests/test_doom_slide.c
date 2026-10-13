#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned reached,cases,finish_cleanup;
static void Done(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==0xa47a || pc==0xa69e || pc==0xa6b6);CHECK(!reached);reached=pc;
    superfx_hook_redirect(fx,0xf800);
}
static void Put(uint8_t *data,unsigned address,uint16_t value) {
    data[address]=(uint8_t)value;data[address+1]=(uint8_t)(value>>8);
}
static uint16_t Multiply(uint16_t a,uint16_t b) {
    return (uint16_t)((uint32_t)((int16_t)a*(int16_t)b)>>15);
}
static void Case(SuperFx *fx,uint8_t *expected,uint16_t vx,uint16_t vy,uint16_t px,uint16_t py,uint16_t count) {
    superfx_reset(fx);reached=0;memset(expected,0xa5,65536);
    Put(expected,0x140,px);Put(expected,0x142,py);Put(expected,0x126,count);Put(expected,0x11c,0x5a8e);
    Put(expected,0x146,vx);Put(expected,0x148,vy);
    memcpy(fx->ram,expected,65536);
    uint16_t magnitude=(uint16_t)(Multiply(vx,py)-Multiply(vy,px));
    uint16_t nx=Multiply(py,magnitude),ny=Multiply((uint16_t)(0-px),magnitude);
    Put(expected,0x12c,nx);Put(expected,0x12e,ny);Put(expected,0x5a9e,nx);Put(expected,0x5aa0,ny);
    if(finish_cleanup) {
        CHECK(count==0);Put(expected,0x5a9e,0);Put(expected,0x5aa0,0);
        Put(expected,0x5ab0,vx);Put(expected,0x5ab2,vy);
    }
    superfx_set_reg(fx,9,vx);superfx_set_reg(fx,10,vy);
    superfx_cpu_write_io(fx,0x301e,0x4f);superfx_cpu_write_io(fx,0x301f,0xa6);
    superfx_sync(fx,fx->master_clock+20000);CHECK(!superfx_is_running(fx));
    unsigned destination=(int16_t)(uint16_t)(count-1)<0 ? (finish_cleanup ? 0xa6b6 : 0xa69e) : 0xa47a;
    if(reached!=destination || superfx_reg(fx,9)!=nx || superfx_reg(fx,10)!=ny || memcmp(fx->ram,expected,65536)) {
        fprintf(stderr,"slide v=%04x,%04x p=%04x,%04x count=%04x expected=%04x,%04x PC=%04x actual=%04x,%04x PC=%04x\n",
            vx,vy,px,py,count,nx,ny,destination,superfx_reg(fx,9),superfx_reg(fx,10),reached);exit(1);
    }
    if(finish_cleanup)CHECK(superfx_reg(fx,11)==0x5ab2);
    cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t entry[]={0x3d,0xa6,0xa0,0xba,0x9f,0x04,0xb6,0x1a,0x4f,0xda,0x13,0x9e,0x23,0x4d,0xc0,0x15};
    CHECK(!memcmp(rom+0x264f,entry,sizeof(entry)));rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    CHECK(superfx_set_pc_hook(fx,0xa47a,Done,NULL));CHECK(superfx_set_pc_hook(fx,0xa69e,Done,NULL));
    const uint16_t vectors[][2]={{0x7fff,0},{0,0x7fff},{0x8000,0},{0,0x8000},{0x5a82,0x5a82},{0x5a82,0xa57e}};
    for(unsigned value=0;value<65536;value++)for(unsigned v=0;v<6;v++)for(unsigned count=0;count<2;count++)
        Case(fx,expected,(uint16_t)value,(uint16_t)~value,vectors[v][0],vectors[v][1],(uint16_t)count);
    const uint16_t boundaries[]={0,1,0x7fff,0x8000,0x8001,0xffff};
    for(unsigned c=0;c<6;c++)for(unsigned vx=0;vx<6;vx++)for(unsigned vy=0;vy<6;vy++)
        Case(fx,expected,boundaries[vx],boundaries[vy],0x5a82,0x5a82,boundaries[c]);
    printf("Retail slide projection/rescan dispatch: %u source-oracle cases passed\n",cases);
    const uint8_t cleanup[]={0x60,0xa6,0x11,0x3d,0xab,0x8e,0x2b,0x56,0x3b,0xdb,0xa6,0x10,
        0x3b,0x3d,0xa0,0xa3,0x2b,0x56,0x3b,0xdb,0xdb,0x3d,0xa0,0xa4,0x3b};
    CHECK(!memcmp(rom+0x269d,cleanup,sizeof(cleanup)));
    CHECK(superfx_set_pc_hook(fx,0xa69e,NULL,NULL));
    CHECK(superfx_set_pc_hook(fx,0xa6b6,Done,NULL));finish_cleanup=1;
    unsigned previous=cases;
    for(unsigned value=0;value<65536;value++)for(unsigned v=0;v<6;v++)
        Case(fx,expected,(uint16_t)value,(uint16_t)~value,vectors[v][0],vectors[v][1],0);
    printf("Retail exhausted-movement cleanup and heights: %u source-oracle cases passed\n",cases-previous);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}
